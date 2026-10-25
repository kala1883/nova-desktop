"""NOVA's SSH bootstrap and queue migration gate; also runs without installed modules.

The native EXE sends this trusted source through stdin. All paths are JSON data.
Only managed project source, queue metadata and staged content are touched here.
"""
import base64
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sqlite3
import struct
import sys
import time
import uuid
import zlib

FILES = ("sync_server.py", "chunksync/__init__.py", "chunksync/common.py",
         "chunksync/wire.py", "chunksync/broker.py", "chunksync/control.py")
PACKET = 128 * 1024
MAX_JSON = 16 * 1024 * 1024
MAX_FILES = 50000
FORMAT = "chunksync-v2"
_MANIFEST_CACHE = {}


class ControlError(ValueError):
    def __init__(self, message, code="admin", state=None):
        super().__init__(message)
        self.code, self.state = code, state


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def flush_directory(path):
    if os.name != "nt":
        descriptor = os.open(path, os.O_RDONLY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)


def atomic(path, value):
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    with temporary.open("xb") as output:
        output.write(canonical(value).encode("utf-8"))
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, path)
    flush_directory(path.parent)


def read_json(path):
    if path.is_symlink():
        raise ControlError("拒绝链接文件 / Linked file refused")
    if path.stat().st_size > MAX_JSON:
        raise ControlError("元数据超限 / Metadata limit exceeded")
    return json.loads(path.read_text(encoding="utf-8"))


def file_manifest(path):
    """Bounded per-channel cache; packet work must not reparse a full queue list."""
    stamp = (path.stat().st_mtime_ns, path.stat().st_size)
    cached = _MANIFEST_CACHE.get(str(path))
    if cached and cached[0] == stamp:
        return cached[1], cached[2]
    values = read_json(path)
    entries = values.get("files")
    if not isinstance(entries, list) or len(entries) > MAX_FILES:
        raise ControlError("迁移文件清单无效 / Invalid migration file list")
    by_name = {entry["name"]: entry for entry in entries}
    if len(by_name) != len(entries):
        raise ControlError("迁移文件重复 / Duplicate migration file")
    if len(_MANIFEST_CACHE) >= 2:
        _MANIFEST_CACHE.clear()
    _MANIFEST_CACHE[str(path)] = (stamp, values, by_name)
    return values, by_name


def identifier(value):
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,80}", value):
        raise ControlError("无效迁移标识 / Invalid migration identifier")
    return value


def digest(path):
    if path.is_symlink() or not path.is_file():
        raise ControlError("拒绝非普通文件 / Regular file required")
    result = hashlib.sha256()
    with path.open("rb") as stream:
        while data := stream.read(1024 * 1024):
            result.update(data)
    return result.hexdigest()


@contextmanager
def sql(path):
    connection = sqlite3.connect(path)
    try:
        with connection:
            yield connection
    finally:
        connection.close()


def paths(project, root):
    if not isinstance(project, str) or not isinstance(root, str):
        raise ControlError("目录格式无效 / Invalid directory")
    project, root = Path(project), Path(root)
    if not project.is_absolute() or not root.is_absolute() or ".." in project.parts or ".." in root.parts:
        raise ControlError("需要绝对目录 / Absolute directories required")
    if not root.resolve().is_relative_to((project / "runtime").resolve()) or root.resolve() == (project / "runtime").resolve():
        raise ControlError("队列必须位于项目 runtime 子目录 / Queue must be below project/runtime")
    if project == Path(project.anchor) or len(str(project)) > 1900 or len(str(root)) > 1900:
        raise ControlError("项目目录无效 / Invalid project directory")
    for item in (root, *root.parents):
        if item.is_symlink() or getattr(item, "is_junction", lambda: False)():
            raise ControlError("目录含链接 / Linked directory refused")
    return project, root


@contextmanager
def queue_lock(root, exclusive=False):
    # Outside the replaceable queue directory, so import cannot change its inode.
    root.parent.mkdir(parents=True, exist_ok=True)
    lock_path = root.parent / (".nova-" + hashlib.sha256(str(root).encode()).hexdigest()[:24] + ".lock")
    if lock_path.is_symlink():
        raise ControlError("锁文件是链接 / Linked lock file")
    with lock_path.open("a+b") as lock:
        if os.name == "nt":  # Isolated Windows fixtures; production targets are Linux.
            import msvcrt
            lock.seek(0)
            lock.write(b"0")
            lock.flush()
            deadline = time.monotonic() + 25
            while True:
                lock.seek(0)
                try:
                    msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
                    break
                except OSError:
                    if time.monotonic() > deadline:
                        raise ControlError("队列忙 / Queue busy")
                    time.sleep(.05)
            try:
                yield
            finally:
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl
            mode = fcntl.LOCK_EX if exclusive else fcntl.LOCK_SH
            deadline = time.monotonic() + 25
            while True:
                try:
                    fcntl.flock(lock, mode | fcntl.LOCK_NB)
                    break
                except BlockingIOError:
                    if time.monotonic() > deadline:
                        raise ControlError("队列忙 / Queue busy")
                    time.sleep(.05)
            try:
                yield
            finally:
                fcntl.flock(lock, fcntl.LOCK_UN)


def control(root, create=False):
    file = root / ".nova-control.json"
    if file.exists():
        value = read_json(file)
        if value.get("version") != 1 or value.get("phase") not in ("ACTIVE", "EXPORTING", "REDIRECT", "IMPORTING", "PREPARED", "ABORTED"):
            raise ControlError("服务端控制记录不兼容 / Incompatible server control record")
        return value
    identity = uuid.uuid4().hex
    value = {"version": 1, "phase": "ACTIVE", "instance": identity, "cluster": identity, "ancestors": []}
    if create:
        atomic(file, value)
    return value


@contextmanager
def active_queue(root):
    with queue_lock(root):
        file = root / ".nova-control.json"
        if file.exists():
            state = control(root)
            if state["phase"] != "ACTIVE":
                code = "moved" if state["phase"] == "REDIRECT" else "frozen"
                raise ControlError("服务器已迁移，请在服务器页连接目标 / Server moved; connect to its destination" if code == "moved" else
                                   "服务器迁移中，队列已冻结；请继续迁移 / Queue frozen; resume migration", code, state)
        yield


def database_status(root):
    db_path = root / "queue.sqlite3"
    if not db_path.exists():
        return {"devices": [], "datasets": [], "bindings": [], "jobs": {}, "stored_bytes": 0}
    if db_path.is_symlink():
        raise ControlError("拒绝链接数据库 / Linked database refused")
    with sql(db_path) as db:
        db.row_factory = sqlite3.Row
        tables = {row[0] for row in db.execute("SELECT name FROM sqlite_master WHERE type='table'")}
        required = {"devices", "datasets", "jobs", "heads", "objects", "job_chunks", "needs", "inventory", "receipts"}
        if not required <= tables:
            raise ControlError("队列数据库格式不兼容 / Incompatible queue schema")
        return {"devices": [dict(r) for r in db.execute("SELECT * FROM devices ORDER BY role")],
                "datasets": [dict(r) for r in db.execute("SELECT * FROM datasets ORDER BY id")],
                "bindings": [dict(r) for r in db.execute("SELECT * FROM bindings")] if "bindings" in tables else [],
                "jobs": {r[0]: r[1] for r in db.execute("SELECT state,count(*) FROM jobs GROUP BY state")},
                "stored_bytes": db.execute("SELECT coalesce(sum(progress),0) FROM objects").fetchone()[0]}


def disk_parent(project):
    while not project.exists():
        project = project.parent
    return project


def probe(project, root):
    recover_import(project, root)
    installed = (root / "broker.json").is_file()
    result = {"installed": installed, "python": sys.version.split()[0],
              "free_bytes": shutil.disk_usage(disk_parent(project)).free, "admin_version": 1}
    if installed:
        with queue_lock(root, True):
            config = read_json(root / "broker.json")
            if not isinstance(config, dict) or config.get("format") != FORMAT:
                raise ControlError("队列格式不兼容 / Incompatible queue format")
            marker = project / ".chunksync-project.json"
            if not marker.is_file() or read_json(marker).get("project") != "NOVA Desktop sync":
                raise ControlError("这不是受管理的项目 / Unmanaged project")
            result.update(database_status(root))
            result["control"] = control(root, True)
            result["quota_bytes"] = config["quota"]
            result["reserve_bytes"] = config["reserve"]
    return result


def managed(project):
    marker = project / ".chunksync-project.json"
    if project.exists() and any(project.iterdir()):
        if not marker.is_file() or read_json(marker).get("project") != "NOVA Desktop sync":
            raise ControlError("拒绝覆盖未知非空目录 / Unknown nonempty project refused")
    project.mkdir(parents=True, exist_ok=True)
    if os.name != "nt":
        project.chmod(0o700)
    atomic(marker, {"project": "NOVA Desktop sync"})


def source_busy(project, root):
    proc = Path("/proc")
    if not proc.exists():
        return False
    entry = str(project / "sync_server.py").encode()
    activated = project / ".nova-active.json"
    since = read_json(activated).get("managed_since", float("inf")) if activated.exists() else float("inf")
    boot_time = None
    try:
        boot_time = next(int(line.split()[1]) for line in (proc / "stat").read_text().splitlines() if line.startswith("btime "))
    except (OSError, StopIteration, ValueError):
        pass
    for item in proc.iterdir():
        if not item.name.isdigit() or int(item.name) == os.getpid():
            continue
        try:
            args = (item / "cmdline").read_bytes().split(b"\0")
            if entry in args and str(root).encode() in args:
                if boot_time is None:
                    return True
                started = boot_time + int((item / "stat").read_text().rsplit(")", 1)[1].split()[19]) / os.sysconf("SC_CLK_TCK")
                if started < since:
                    return True
        except (OSError, PermissionError):
            pass
    return False


SHIM = '''# NOVA managed entry point; versioned source is activated atomically.
import json,os,pathlib,re,runpy,sys
p=pathlib.Path(__file__).resolve().parent
r=json.loads((p/'.nova-active.json').read_text(encoding='utf-8'))['release']
if not re.fullmatch('[a-f0-9]{64}',r): raise SystemExit('Invalid release')
os.environ['NOVA_SYNC_PROJECT']=str(p)
sys.path.insert(0,str(p/'releases'/r))
runpy.run_path(str(p/'releases'/r/'sync_server.py'),run_name='__main__')
'''


def install(project, root, request, payload):
    managed(project)
    recover_import(project, root)
    token = identifier(request.get("token"))
    directory = project / (".nova-install-" + token)
    directory.mkdir(exist_ok=True)
    if directory.is_symlink():
        raise ControlError("安装目录是链接 / Linked installation directory")
    action = request["action"]
    if action == "install_begin":
        files = request.get("files")
        if not isinstance(files, list) or {f.get("name") for f in files} != set(FILES) or len(files) != len(FILES):
            raise ControlError("安装清单无效 / Invalid installation manifest")
        for item in files:
            if type(item.get("size")) is not int or not 0 <= item["size"] <= 1024 * 1024 or not re.fullmatch("[a-f0-9]{64}", item.get("sha256", "")):
                raise ControlError("安装文件超限 / Invalid installation file")
        manifest = directory / "manifest.json"
        if manifest.exists() and read_json(manifest) != files:
            raise ControlError("安装标识冲突 / Installation identifier conflict")
        atomic(manifest, files)
        return {"ready": True}, b""
    files = read_json(directory / "manifest.json")
    if action == "install_put":
        item = next((f for f in files if f["name"] == request.get("name")), None)
        if not item:
            raise ControlError("文件不在安装清单 / File not in manifest")
        return put_file(directory / item["name"], item["size"], item["sha256"], request.get("offset"), payload), b""
    if action != "install_commit":
        raise ControlError("未知安装操作 / Unknown installation action")
    for item in files:
        file = directory / item["name"]
        if file.stat().st_size != item["size"] or digest(file) != item["sha256"]:
            raise ControlError("安装校验失败 / Installation checksum mismatch")
        compile(file.read_bytes(), item["name"], "exec")
    release = hashlib.sha256(canonical(files).encode()).hexdigest()
    releases = project / "releases"
    releases.mkdir(exist_ok=True)
    if releases.is_symlink():
        raise ControlError("版本目录是链接 / Linked releases directory")
    with queue_lock(root, True):
        if source_busy(project, root):
            raise ControlError("服务器正在同步，请暂停后重试部署 / Active server sessions; pause and retry deployment")
        target = releases / release
        if not target.exists():
            os.replace(directory, target)
            flush_directory(releases)
        os.environ["NOVA_SYNC_PROJECT"] = str(project)
        sys.path.insert(0, str(target))
        # A bootstrap connection only installs one version; no inherited module cache.
        from chunksync.broker import Broker
        quota, reserve = request.get("quota_gib", 15), request.get("reserve_gib", 5)
        if type(quota) is not int or type(reserve) is not int or not 1 <= quota <= 1048576 or not 0 <= reserve <= 1048576:
            raise ControlError("配额无效 / Invalid quota")
        if (root / "broker.json").exists() and not (root / "queue.sqlite3").is_file():
            raise ControlError("已有队列缺失数据库，拒绝重置记录 / Existing queue database is missing; refusing to reset records")
        Broker(root, initialize=True, quota=quota * 2**30, reserve=reserve * 2**30)
        if os.name != "nt":
            root.chmod(0o700)
        control(root, True)
        shim = project / "sync_server.py"
        temporary = project / (".nova-shim-" + token)
        with temporary.open("wb") as output:
            output.write(SHIM.encode())
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, shim)
        active_file = project / ".nova-active.json"
        since = read_json(active_file).get("managed_since", time.time()) if active_file.exists() else time.time()
        atomic(active_file, {"release": release, "managed_since": since})
    return probe(project, root), b""


def put_file(path, size, sha256, offset, payload):
    path.parent.mkdir(parents=True, exist_ok=True)
    for ancestor in (path, *path.parents):
        if ancestor.is_symlink():
            raise ControlError("拒绝链接文件 / Linked file refused")
    current = path.stat().st_size if path.exists() else 0
    if offset is None:
        if size == 0 and not path.exists():
            with path.open("xb") as output:
                output.flush()
                os.fsync(output.fileno())
        if current == size and digest(path) != sha256:
            with path.open("wb"):
                pass
            current = 0
        return {"offset": current}
    if type(offset) is not int or offset != current or not payload or offset + len(payload) > size:
        raise ControlError("上传断点不匹配 / Upload offset mismatch")
    with path.open("ab") as output:
        output.write(payload)
        output.flush()
        os.fsync(output.fileno())
    current += len(payload)
    if current == size and digest(path) != sha256:
        raise ControlError("文件校验失败 / File checksum mismatch")
    return {"offset": current}


def migration_file(name):
    if name in ("queue.sqlite3", "broker.json") or re.fullmatch(r"chunks/[a-f0-9]{64}|uploads/[a-f0-9]{64}\.part", name or ""):
        return name
    raise ControlError("迁移文件名无效 / Invalid migration file")


def export(project, root, request):
    token = identifier(request.get("token"))
    action = request["action"]
    folder = root / (".nova-export-" + token)
    with queue_lock(root, True):
        state = control(root, True)
        if action == "export_abort":
            if state["phase"] == "ACTIVE":
                return {"restored": True}, b""
            if state["phase"] != "EXPORTING" or state.get("token") != token:
                raise ControlError("切换已提交，不能恢复旧端 / Cutover committed; old server cannot be restored")
            state["phase"] = "ACTIVE"
            atomic(root / ".nova-control.json", state)
            return {"restored": True}, b""
        if action == "export_begin":
            destination = request.get("destination")
            if not isinstance(destination, dict) or not identifier(destination.get("instance")):
                raise ControlError("目标无效 / Invalid destination")
            if state["phase"] == "ACTIVE":
                status = database_status(root)
                if not any(d["role"] == request.get("device") and d["node_id"] == request.get("node_id") for d in status["devices"]):
                    raise ControlError("源设备身份不匹配 / Source device identity mismatch")
                if folder.exists():
                    # A restored source may have changed; never reuse its old snapshot.
                    os.replace(folder, folder.with_name(folder.name + ".retained-" + uuid.uuid4().hex))
                state.update(phase="EXPORTING", token=token, destination=destination)
                atomic(root / ".nova-control.json", state)  # Gate commits before snapshot.
            elif state.get("token") != token or state.get("destination") != destination or state["phase"] not in ("EXPORTING", "REDIRECT"):
                raise ControlError("另一个迁移尚未完成 / Another migration is pending")
            folder.mkdir(exist_ok=True)
            manifest_path = folder / "manifest.json"
            if not manifest_path.exists():
                with sql(root / "queue.sqlite3") as source, sql(folder / "queue.sqlite3") as backup:
                    source.backup(backup)
                with (folder / "queue.sqlite3").open("r+b") as stream:
                    os.fsync(stream.fileno())
                shutil.copyfile(root / "broker.json", folder / "broker.json")
                with (folder / "broker.json").open("r+b") as stream:
                    os.fsync(stream.fileno())
                entries = [{"name": name, "size": (folder / name).stat().st_size} for name in ("broker.json", "queue.sqlite3")]
                for category in ("chunks", "uploads"):
                    for path in sorted((root / category).iterdir()):
                        name = migration_file(category + "/" + path.name)
                        if path.is_symlink() or not path.is_file():
                            raise ControlError("拒绝非普通迁移文件 / Invalid migration file")
                        entries.append({"name": name, "size": path.stat().st_size})
                        if len(entries) > MAX_FILES:
                            raise ControlError("迁移文件数量超限 / Migration file limit exceeded")
                manifest = {"files": entries, "control": state, "bytes": sum(e["size"] for e in entries)}
                if len(canonical(manifest).encode()) > MAX_JSON - 4096:
                    raise ControlError("迁移清单超限 / Migration manifest too large")
                atomic(manifest_path, manifest)
            manifest = read_json(manifest_path)
            manifest["control"] = state
            return manifest, b""
        if state.get("token") != token or state["phase"] not in ("EXPORTING", "REDIRECT"):
            raise ControlError("迁移快照不可用 / Migration snapshot unavailable")
        if action == "export_commit":
            if request.get("instance") != state["destination"]["instance"]:
                raise ControlError("目标身份改变 / Destination identity changed")
            state["phase"] = "REDIRECT"
            atomic(root / ".nova-control.json", state)
            return state, b""
        name = migration_file(request.get("name"))
        _, entries = file_manifest(folder / "manifest.json")
        item = entries.get(name)
        if item is None:
            raise ControlError("文件不在快照内 / File not in snapshot")
        path = (folder if name in ("queue.sqlite3", "broker.json") else root) / name
        if path.stat().st_size != item["size"] or path.is_symlink():
            raise ControlError("快照文件改变 / Snapshot file changed")
        if action == "export_info":
            sha = digest(path)
            if name.startswith("chunks/") and sha != path.name:
                raise ControlError("源内容块损坏，未切换 / Source chunk is corrupt; cutover stopped")
            return {**item, "sha256": sha}, b""
        if action == "export_get":
            offset = request.get("offset")
            if type(offset) is not int or not 0 <= offset <= item["size"]:
                raise ControlError("下载断点无效 / Invalid download offset")
            with path.open("rb") as stream:
                stream.seek(offset)
                data = stream.read(PACKET)
            return {"offset": offset}, data
    raise ControlError("未知导出操作 / Unknown export action")


def staging(project, token):
    folder = project / "runtime" / (".nova-import-" + identifier(token))
    if folder.is_symlink():
        raise ControlError("迁移目录是链接 / Linked migration directory")
    folder.mkdir(parents=True, exist_ok=True)
    if os.name != "nt":
        folder.chmod(0o700)
    return folder


def recover_import(project, root):
    if root.exists() or not (project / "runtime").exists():
        return
    candidates = []
    for folder in (project / "runtime").glob(".nova-import-*"):
        value = folder / "queue" / ".nova-control.json"
        if value.is_file():
            state = read_json(value)
            if state.get("phase") == "PREPARED" and state.get("root") == str(root):
                candidates.append(folder / "queue")
    if len(candidates) > 1:
        raise ControlError("存在多个恢复快照 / Multiple recovery snapshots")
    if candidates:
        os.replace(candidates[0], root)
        flush_directory(root.parent)


def import_data(project, root, request, payload):
    token, action = identifier(request.get("token")), request["action"]
    folder = staging(project, token)
    recover_import(project, root)
    with queue_lock(root, True):
        state = control(root, True)
        if action == "import_begin":
            source = request.get("source")
            entries = request.get("files")
            if not isinstance(source, dict) or not isinstance(entries, list) or not 2 <= len(entries) <= MAX_FILES:
                raise ControlError("迁移清单格式无效 / Invalid migration manifest")
            if state["phase"] == "ACTIVE" and state.get("token") == token:
                return state, b""  # Recover after activation but before local settings commit.
            if state["phase"] == "ACTIVE":
                current = database_status(root)
                if current["devices"] or current["datasets"] or current["jobs"] or current["stored_bytes"]:
                    raise ControlError("目标已有同步记录，拒绝覆盖 / Destination contains synchronization records")
                reserve = read_json(root / "broker.json")["reserve"]
                required = sum(e["size"] for e in entries)
                if shutil.disk_usage(root).free < required + reserve:
                    raise ControlError("目标空间不足 / Destination free space insufficient")
                ancestors = list(source.get("ancestors", [])) + [source["instance"]]
                if len(ancestors) > 64:
                    raise ControlError("迁移链超限 / Migration lineage limit reached")
                state.update(phase="IMPORTING", token=token, cluster=source["cluster"], ancestors=ancestors,
                             previous=source["instance"], root=str(root))
                atomic(root / ".nova-control.json", state)
            elif state.get("token") != token or state["phase"] not in ("IMPORTING", "PREPARED"):
                raise ControlError("目标存在另一项迁移 / Destination has another migration")
            metadata = folder / "transfer.json"
            values = {"source": {k: source[k] for k in ("instance", "cluster", "ancestors")}, "files": entries}
            for entry in entries:
                migration_file(entry["name"])
                if type(entry["size"]) is not int or entry["size"] < 0:
                    raise ControlError("迁移大小无效 / Invalid migration size")
            if len({e["name"] for e in entries}) != len(entries):
                raise ControlError("重复迁移文件 / Duplicate migration file")
            if metadata.exists() and read_json(metadata) != values:
                raise ControlError("迁移快照与断点不一致 / Snapshot differs from checkpoint")
            atomic(metadata, values)
            with sql(folder / "transfer.sqlite3") as db:
                db.execute("CREATE TABLE IF NOT EXISTS files(name TEXT PRIMARY KEY,size INTEGER,sha256 TEXT)")
            return state, b""
        if state.get("token") != token:
            raise ControlError("目标迁移标识不匹配 / Destination migration mismatch")
        if action == "import_activate":
            if state["phase"] == "ACTIVE":
                return probe_unlocked(root), b""
            if state["phase"] != "PREPARED" or request.get("previous") != state["previous"]:
                raise ControlError("目标未就绪 / Destination not prepared")
            state["phase"] = "ACTIVE"
            atomic(root / ".nova-control.json", state)
            return probe_unlocked(root), b""
        if state["phase"] == "ACTIVE":
            return state, b""
        if action == "import_prepare":
            if state["phase"] == "PREPARED":
                return state, b""
            values, _ = file_manifest(folder / "transfer.json")
            with sql(folder / "transfer.sqlite3") as db:
                rows = {r[0]: r[1:] for r in db.execute("SELECT name,size,sha256 FROM files")}
            for item in values["files"]:
                record = rows.get(item["name"])
                path = folder / "queue" / item["name"]
                # Per-file completion hashes were verified before this final step.
                if not record or record[0] != item["size"] or not path.is_file() or path.stat().st_size != item["size"]:
                    raise ControlError("迁移文件未完整接收 / Migration files incomplete")
            queue = folder / "queue"
            if os.name != "nt":
                queue.chmod(0o700)
            with sql(queue / "queue.sqlite3") as db:
                if db.execute("PRAGMA integrity_check").fetchone()[0] != "ok":
                    raise ControlError("迁移数据库校验失败 / Migrated database integrity check failed")
            status = database_status(queue)
            if read_json(queue / "broker.json").get("format") != FORMAT:
                raise ControlError("迁移配置格式不兼容 / Incompatible migrated configuration")
            (queue / "chunks").mkdir(exist_ok=True)
            (queue / "uploads").mkdir(exist_ok=True)
            state["phase"] = "PREPARED"
            atomic(queue / ".nova-control.json", state)
            backup = root.with_name(root.name + ".nova-before-" + token)
            if backup.exists():
                raise ControlError("恢复备份已存在，请继续迁移 / Recovery backup exists; resume migration")
            os.replace(root, backup)
            os.replace(queue, root)
            flush_directory(root.parent)
            return {**state, "devices": status["devices"]}, b""
        if action == "import_put":
            if state["phase"] == "PREPARED":
                return {"offset": request["size"]}, b""
            name = migration_file(request.get("name"))
            size, sha = request.get("size"), request.get("sha256")
            _, entries = file_manifest(folder / "transfer.json")
            if entries.get(name) != {"name": name, "size": size} or not re.fullmatch("[a-f0-9]{64}", sha or ""):
                raise ControlError("迁移文件不在清单 / File not in migration manifest")
            path = folder / "queue" / name
            with sql(folder / "transfer.sqlite3") as db:
                old = db.execute("SELECT size,sha256 FROM files WHERE name=?", (name,)).fetchone()
                if old and old != (size, sha):
                    raise ControlError("迁移块身份改变 / Migration file identity changed")
                answer = put_file(path, size, sha, request.get("offset"), payload)
                if answer["offset"] == size:
                    if digest(path) != sha:
                        raise ControlError("迁移校验失败 / Migration checksum mismatch")
                    db.execute("INSERT OR REPLACE INTO files VALUES (?,?,?)", (name, size, sha))
            return answer, b""
    raise ControlError("未知导入操作 / Unknown import action")


def probe_unlocked(root):
    result = database_status(root)
    result.update(installed=True, control=control(root), quota_bytes=read_json(root / "broker.json")["quota"])
    return result


def handle(request, payload=b""):
    if sys.version_info < (3, 9):
        raise ControlError("服务器需要 Python 3.9+ / Server requires Python 3.9+")
    if not isinstance(request, dict):
        raise ControlError("请求格式无效 / Invalid request")
    project, root = paths(request.get("project"), request.get("root"))
    action = request.get("action", "")
    if action == "probe":
        return probe(project, root), b""
    if action.startswith("install_"):
        return install(project, root, request, payload)
    if action.startswith("export_"):
        return export(project, root, request)
    if action.startswith("import_"):
        return import_data(project, root, request, payload)
    raise ControlError("不支持的管理操作 / Unsupported administration action")


def exact(stream, length):
    data = bytearray()
    while len(data) < length:
        block = stream.read(length - len(data))
        if not block:
            raise EOFError
        data.extend(block)
    return bytes(data)


def serve(reader, writer):
    while True:
        try:
            length, count = struct.unpack("!II", exact(reader, 8))
            if not 0 < length <= MAX_JSON or count > PACKET:
                raise ControlError("帧超限 / Frame limit exceeded")
            request = json.loads(exact(reader, length).decode("utf-8"))
            data = exact(reader, count)
            raw = request.pop("_raw_size", 0)
            encoding = request.pop("_encoding", None)
            if type(raw) is not int or not 0 <= raw <= PACKET:
                raise ControlError("数据包超限 / Packet limit exceeded")
            if encoding == "zlib":
                decoder = zlib.decompressobj()
                data = decoder.decompress(data, raw + 1)
                if not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
                    raise ControlError("压缩帧无效 / Invalid compressed frame")
            elif encoding is not None:
                raise ControlError("压缩类型不支持 / Unsupported encoding")
            if len(data) != raw:
                raise ControlError("数据长度无效 / Invalid data length")
            result, data = handle(request, data)
            response = {"ok": True, "result": result}
        except EOFError:
            return
        except (ValueError, OSError, sqlite3.Error, KeyError, TypeError) as error:
            response, data = {"ok": False, "error": str(error)[:1200], "code": getattr(error, "code", "admin")}, b""
        if data:
            response["_raw_size"] = len(data)
        encoded = canonical(response).encode("utf-8")
        writer.write(struct.pack("!II", len(encoded), len(data)))
        writer.write(encoded)
        writer.write(data)
        writer.flush()


if __name__ == "__main__":
    serve(sys.stdin.buffer, sys.stdout.buffer)
