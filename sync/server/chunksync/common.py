import hashlib
import json
import os
from pathlib import Path
import re
import sqlite3
from contextlib import contextmanager
import unicodedata
import uuid

PROJECT = Path(os.environ.get("NOVA_SYNC_PROJECT", Path(__file__).resolve().parents[1]))
MAX_CHUNK = 2 * 1024 * 1024
PACKET = 128 * 1024
FORMAT = "chunksync-v2"


class SyncError(ValueError):
    pass


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def digest_bytes(value):
    return hashlib.sha256(value).hexdigest()


def manifest_id(value):
    return digest_bytes(canonical(value).encode("utf-8"))


def checked_id(value):
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,80}", value):
        raise SyncError("无效的标识符")
    return value


def checked_hash(value):
    if not isinstance(value, str) or not re.fullmatch(r"[a-f0-9]{64}", value):
        raise SyncError("无效的 SHA-256")
    return value


def relative_key(value):
    if not isinstance(value, str) or not value or unicodedata.normalize("NFC", value) != value:
        raise SyncError("文件路径必须是 NFC 格式的相对路径")
    if any(ch in value for ch in '<>:"\\|?*'):
        raise SyncError("文件路径含不支持的字符")
    for part in value.split("/"):
        if part in ("", ".", "..", ".git") or part.endswith((" ", ".")):
            raise SyncError("拒绝越界路径或 Git 内部目录")
        if any(ord(ch) < 32 for ch in part) or re.fullmatch(r"CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9]", part.split(".")[0], re.I):
            raise SyncError("不兼容的文件名")
    return value.casefold()


def linked(path):
    return path.is_symlink() or getattr(path, "is_junction", lambda: False)()


def safe_path(root, relative):
    relative_key(relative)
    root = Path(root).resolve()
    target = root
    for part in relative.split("/"):
        target = target / part
        if linked(target):
            raise SyncError("同步目录内不跟随符号链接或 junction")
    if not target.resolve().is_relative_to(root):
        raise SyncError("路径超出同步目录")
    return target


def fsync_dir(path):
    if os.name != "nt":
        fd = os.open(path, os.O_RDONLY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)


def atomic_json(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    with temporary.open("x", encoding="utf-8") as output:
        output.write(canonical(data) + "\n")
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, path)
    fsync_dir(path.parent)


def hash_file(path):
    path = Path(path)
    if linked(path) or not path.is_file():
        raise SyncError("目标不是普通文件")
    sha = hashlib.sha256()
    count = 0
    with path.open("rb") as stream:
        while data := stream.read(1024 * 1024):
            sha.update(data)
            count += len(data)
    return sha.hexdigest(), count


def cache_chunk(directory, data):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    digest = digest_bytes(data)
    target = directory / digest
    if target.exists():
        if hash_file(target) != (digest, len(data)):
            raise SyncError("本机内容缓存校验失败")
        return digest
    temporary = directory / (uuid.uuid4().hex + ".tmp")
    with temporary.open("xb") as output:
        output.write(data)
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, target)
    fsync_dir(directory)
    return digest


@contextmanager
def database(path, write=False, max_bytes=None):
    connection = sqlite3.connect(path, timeout=30, isolation_level=None)
    connection.row_factory = sqlite3.Row
    connection.execute("PRAGMA foreign_keys=ON")
    connection.execute("PRAGMA synchronous=FULL")
    if max_bytes:
        page_size = connection.execute("PRAGMA page_size").fetchone()[0]
        connection.execute(f"PRAGMA max_page_count={max_bytes // page_size}")
    try:
        if write:
            connection.execute("BEGIN IMMEDIATE")
        yield connection
        if write:
            connection.commit()
    except BaseException:
        if write:
            connection.rollback()
        raise
    finally:
        connection.close()
