"""Length-prefixed JSON/binary messages over an authenticated SSH stdio stream."""

import json
import struct
import zlib

from .common import PACKET, SyncError, canonical

HEADER = struct.Struct("!II")
MAX_JSON = 16 * 1024 * 1024


def read_exact(stream, count):
    data = bytearray()
    while len(data) < count:
        block = stream.read(count - len(data))
        if not block:
            raise EOFError("SSH 数据流已断开")
        data.extend(block)
    return bytes(data)


def write_message(stream, message, payload=b""):
    if len(payload) > PACKET:
        raise SyncError("数据包过大")
    message = dict(message)
    encoded = payload
    if payload:
        compressed = zlib.compress(payload, 1)
        if len(compressed) < len(payload) * 0.95:
            encoded = compressed
            message["_encoding"] = "zlib"
        message["_raw_size"] = len(payload)
    header = canonical(message).encode("utf-8")
    if len(header) > MAX_JSON:
        raise SyncError("清单过大")
    stream.write(HEADER.pack(len(header), len(encoded)))
    stream.write(header)
    stream.write(encoded)
    stream.flush()
    return len(encoded)


def read_message(stream):
    json_size, binary_size = HEADER.unpack(read_exact(stream, HEADER.size))
    if not 0 < json_size <= MAX_JSON or binary_size > PACKET:
        raise SyncError("不合法的数据帧长度")
    message = json.loads(read_exact(stream, json_size).decode("utf-8"))
    if not isinstance(message, dict):
        raise SyncError("数据帧必须包含 JSON 对象")
    encoded = read_exact(stream, binary_size)
    raw_size = message.pop("_raw_size", 0)
    encoding = message.pop("_encoding", None)
    if type(raw_size) is not int or not 0 <= raw_size <= PACKET:
        raise SyncError("解压后长度超限")
    if encoding == "zlib":
        decoder = zlib.decompressobj()
        payload = decoder.decompress(encoded, raw_size + 1)
        if not decoder.eof or decoder.unconsumed_tail or decoder.unused_data:
            raise SyncError("不合法的压缩数据")
    elif encoding is None:
        payload = encoded
    else:
        raise SyncError("不支持的压缩编码")
    if len(payload) != raw_size:
        raise SyncError("数据包长度校验失败")
    return message, payload, len(encoded)
