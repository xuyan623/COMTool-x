"""Original COMTool ASCII/binary plot protocols plus editable Python codecs."""

from __future__ import annotations

import json
import math
import re
import struct

from ..sdk import Definition, NodeRuntime, Port, WorkbenchError


def encode_text(text, encoding="utf-8", hex_mode=False, escape=False, newline=""):
    """Encode text and byte escapes without corrupting non-ASCII characters."""
    if hex_mode:
        result = bytes.fromhex(text)
    elif escape:
        result = bytearray()
        offset = 0
        escapes = {r"\n": b"\n", r"\r": b"\r", r"\t": b"\t",
                   r"\\": b"\\", r"\0": b"\0"}
        for match in re.finditer(r"\\(?:x[0-9a-fA-F]{2}|[nrt\\0])", text):
            result.extend(text[offset:match.start()].encode(encoding))
            token = match.group()
            result.extend(bytes([int(token[2:], 16)]) if token.startswith(r"\x")
                          else escapes[token])
            offset = match.end()
        result.extend(text[offset:].encode(encoding))
        result = bytes(result)
    else:
        result = text.encode(encoding)
    return result + {"None": b"", "LF": b"\n", "CRLF": b"\r\n", "": b""}[newline]


def crc16(data, initial=0xFFFF):
    """Modbus CRC16 for the same custom-protocol use case as upstream COMTool."""
    value = initial
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ 0xA001 if value & 1 else value >> 1
    return value


class PlotDecoder(NodeRuntime):
    """Incrementally decode complete frames, preserving partial frames across reads."""

    def __init__(self, node, engine):
        super().__init__(node, engine)
        self.buffer = bytearray()
        self.state.update(frames=0, bad_frames=0, buffered_bytes=0)
        self.header = bytes.fromhex(self.config["header"])
        if not self.header:
            raise WorkbenchError("二进制帧头不能为空")

    def receive(self, port, data):
        if not isinstance(data, bytes):
            raise WorkbenchError("曲线协议需要 bytes")
        self.buffer.extend(data)
        output = {}
        while self.buffer:
            header = self.header if self.config["binary"] else b"$"
            position = self.buffer.find(header)
            if position < 0:
                # Retain only a possible partial header at the chunk boundary.
                retain = max(0, len(header) - 1)
                self.buffer[:] = self.buffer[-retain:] if retain else b""
                break
            del self.buffer[:position]
            if self.config["binary"]:
                if len(self.buffer) < len(header) + 1:
                    break
                name_length = self.buffer[len(header)]
                frame_length = len(header) + 1 + name_length + 16 + 1
                if len(self.buffer) < frame_length:
                    break
                frame = bytes(self.buffer[:frame_length])
                try:
                    if not name_length or sum(frame[:-1]) % 256 != frame[-1]:
                        raise ValueError("二进制帧校验失败")
                    name = frame[len(header) + 1:-17].decode("utf-8")
                    x, y = struct.unpack("<dd", frame[-17:-1])
                except (ValueError, UnicodeError, struct.error):
                    with self.lock:
                        self.state["bad_frames"] += 1
                    del self.buffer[:1]
                    continue
                del self.buffer[:frame_length]
            else:
                end = self.buffer.find(b"\n")
                if end < 0:
                    if len(self.buffer) > 65536:
                        self.buffer.clear()
                        with self.lock:
                            self.state["bad_frames"] += 1
                    break
                frame = bytes(self.buffer[:end]).rstrip(b"\r")
                del self.buffer[:end + 1]
                # A fresh header supersedes an incomplete old line after reconnect.
                frame = frame[frame.rfind(b"$"):]
                parts = frame[1:].split(b",")
                try:
                    if len(parts) not in (3, 4):
                        raise ValueError("ASCII 帧字段数不正确")
                    if len(parts) == 4 and sum(frame[:frame.rfind(b",")]) % 256 != int(parts[3]):
                        raise ValueError("ASCII 帧校验失败")
                    name = parts[0].decode("utf-8")
                    x, y = float(parts[1]), float(parts[2])
                except (ValueError, UnicodeError):
                    with self.lock:
                        self.state["bad_frames"] += 1
                    continue
            if not math.isfinite(x) or not math.isfinite(y):
                with self.lock:
                    self.state["bad_frames"] += 1
                continue
            channel = output.setdefault(name, {"x": [], "y": []})
            channel["x"].append(x)
            channel["y"].append(y)
            with self.lock:
                self.state["frames"] += 1
        with self.lock:
            self.state["buffered_bytes"] = len(self.buffer)
        if output:
            return self.emit("samples", output)

    def action(self, name, args):
        if name == "clear":
            self.buffer.clear()
            with self.lock:
                self.state.update(frames=0, bad_frames=0, buffered_bytes=0)
            return self.snapshot()
        return super().action(name, args)


class PythonProtocol(NodeRuntime):
    """Execute explicitly configured local protocol code with persistent codec state."""

    def start(self):
        namespace = {"struct": struct, "pack": struct.pack, "unpack": struct.unpack,
                     "json": json, "math": math, "crc16": crc16, "state": {}}
        exec(compile(self.config["code"], f"<protocol:{self.id}>", "exec"), namespace)
        method = namespace.get(self.config["operation"])
        if not callable(method):
            raise WorkbenchError(f"协议代码需要定义 {self.config['operation']}(data)")
        self.method = method
        super().start()

    def receive(self, port, data):
        result = self.method(data)
        if result is not None:
            if not isinstance(result, bytes):
                raise WorkbenchError("自定义协议函数必须返回 bytes 或 None")
            return self.emit("out", result)


class Encoder(NodeRuntime):
    def receive(self, port, data):
        cfg = self.config
        if cfg["format"] == "json":
            encoded = (json.dumps(data, ensure_ascii=False, allow_nan=False)
                       + {"None": "", "LF": "\n", "CRLF": "\r\n"}[cfg["newline"]]).encode(cfg["encoding"])
        else:
            value = data.get("value", data) if isinstance(data, dict) else data
            text = cfg["template"].format(value=value)
            encoded = encode_text(text, cfg["encoding"], cfg["format"] == "hex",
                                  cfg["escape"], cfg["newline"])
        return self.emit("bytes", encoded)


class TextDecoder(NodeRuntime):
    def __init__(self, node, engine):
        super().__init__(node, engine)
        import codecs
        self.decoder = codecs.getincrementaldecoder(self.config["encoding"])("replace")

    def receive(self, port, data):
        return self.emit("text", self.decoder.decode(data))


def register(registry):
    registry.register(Definition("protocol.plot", "曲线协议解码", "协议",
        "保留 COMTool $name,x,y[,checksum] 和二进制帧协议；支持粘包、分包",
        (Port("bytes", "原始字节", "in", "bytes"), Port("samples", "曲线数据", "out", "samples")),
        {"binary": {"type": "boolean", "default": False},
         "header": {"type": "string", "default": "AA CC EE BB"}},
        PlotDecoder, actions={"clear": {"description": "清空解析缓冲和统计"}}))
    registry.register(Definition("protocol.python", "Python 协议", "协议",
        "编辑 encode/decode(data) 函数；提供 struct、crc16、json 和持久 state",
        (Port("in", "输入字节", "in", "bytes"), Port("out", "输出字节", "out", "bytes")),
        {"operation": {"type": "string", "default": "decode", "choices": ["encode", "decode"]},
         "code": {"type": "string", "multiline": True,
                  "default": "def decode(data):\n    return data\n\ndef encode(data):\n    return data\n"}},
        PythonProtocol))
    registry.register(Definition("protocol.encode", "数据编码", "协议", "数值/文本/事件编码为文本、HEX 或 JSON",
        (Port("value", "输入值", "in", "any"), Port("bytes", "编码字节", "out", "bytes")),
        {"format": {"type": "string", "default": "text", "choices": ["text", "hex", "json"]},
         "template": {"type": "string", "default": "{value}", "multiline": True},
         "encoding": {"type": "string", "default": "utf-8"},
         "escape": {"type": "boolean", "default": True},
         "newline": {"type": "string", "default": "LF", "choices": ["None", "LF", "CRLF"]}},
        Encoder))
    registry.register(Definition("protocol.text", "文本解码", "协议", "增量字符解码，保留跨数据块的多字节字符",
        (Port("bytes", "字节", "in", "bytes"), Port("text", "文本", "out", "text")),
        {"encoding": {"type": "string", "default": "utf-8"}}, TextDecoder))
