"""Protocol 3 worker, executed by the user's selected local Python interpreter.

The native application owns graph order and committed state. This process runs
only Python extensions and may be terminated on timeout without stopping Qt.
No interpreter, package installer or third-party module is bundled here.
"""

import base64
import contextlib
import importlib
import importlib.util
import io
import hashlib
import json
import math
import struct
import sys
import tempfile
import traceback
from pathlib import Path

PROTOCOL = 3
MAX_MESSAGE = 16 * 1024 * 1024


class CapturedLog:
    """Bound plugin print output without mixing it into the reply protocol."""

    def __init__(self):
        self.stream, self.remaining, self.truncated = io.StringIO(), 65536, False

    def write(self, text):
        text = str(text)
        self.stream.write(text[:self.remaining])
        self.truncated |= len(text) > self.remaining
        self.remaining = max(0, self.remaining - len(text))
        return len(text)

    def flush(self):
        # StringIO writes immediately; this stream has no device buffer.
        return None

    def value(self):
        return self.stream.getvalue() + ("\n[Python 日志超过 64 KiB，已截断]" if self.truncated else "")


def unload(namespaces, node):
    """Remove only this plugin's temporary package and imported submodules."""
    old = namespaces.pop(node, None)
    if old is not None:
        prefix = old["package"]
        for name in list(sys.modules):
            if name == prefix or name.startswith(prefix + "."):
                del sys.modules[name]
        old["directory"].cleanup()


def load_namespace(request, namespaces):
    """Expand portable resources into an isolated package for relative imports."""
    node = request["node"]
    unload(namespaces, node)
    package = "corig_ext_" + hashlib.sha256(node.encode()).hexdigest()[:16]
    directory = tempfile.TemporaryDirectory(prefix="corig-python-")
    root = Path(directory.name).resolve()
    try:
        for name, content in request.get("resources", {}).items():
            relative = Path(name)
            target = (root / relative).resolve()
            if relative.is_absolute() or not target.is_relative_to(root):
                raise ValueError("插件资源不能越出所在目录：" + name)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content if isinstance(content, bytes) else str(content).encode("utf-8"))
        source = root / "__init__.py"
        source.write_text(request["code"], encoding="utf-8")
        specification = importlib.util.spec_from_file_location(package, source, submodule_search_locations=[str(root)])
        module = importlib.util.module_from_spec(specification)
        namespace = module.__dict__
        namespace.update({"__package__": package, "__path__": [str(root)], "__file__": str(source),
                          "math": math, "json": json, "struct": struct, "crc16": crc16,
                          "resources": request.get("resources", {})})
        sys.modules[package] = module
        namespaces[node] = {"package": package, "directory": directory, "namespace": namespace}
        exec(compile(request["code"], str(source), "exec"), namespace)
        return namespace
    except Exception:
        unload(namespaces, node)
        directory.cleanup()
        raise


def decode(value):
    """Restore explicit byte buffers without guessing at ordinary text values."""
    if isinstance(value, dict):
        if value.get("type") == "bytes" and "base64" in value:
            return base64.b64decode(value["base64"], validate=True)
        return {key: decode(item) for key, item in value.items()}
    if isinstance(value, list):
        return [decode(item) for item in value]
    return value


def encode(value):
    """Commit only portable, finite results and explicit script state."""
    if isinstance(value, (bytes, bytearray, memoryview)):
        value = bytes(value)
        return {"type": "bytes", "base64": base64.b64encode(value).decode("ascii"), "length": len(value)}
    if value is None or isinstance(value, (str, bool, int)):
        return value
    if isinstance(value, float):
        if not math.isfinite(value):
            raise ValueError("Python 输出和状态不能包含 NaN 或无穷大")
        return value
    if isinstance(value, dict):
        return {str(key): encode(item) for key, item in value.items()}
    if isinstance(value, (tuple, list)):
        return [encode(item) for item in value]
    if hasattr(value, "tolist"):
        return encode(value.tolist())
    raise TypeError("Python 输出或状态包含不可保存的对象：" + type(value).__name__)


def crc16(data, initial=0xFFFF):
    """Provide the same Modbus CRC helper to user codecs on every local runtime."""
    result = initial
    for byte in data:
        result ^= byte
        for _ in range(8):
            result = (result >> 1) ^ 0xA001 if result & 1 else result >> 1
    return result


def reply(value):
    """Keep protocol output separate from plugin print/log output."""
    payload = json.dumps(encode(value), ensure_ascii=False, allow_nan=False, separators=(",", ":"))
    if len(payload.encode("utf-8")) > MAX_MESSAGE:
        raise ValueError("单次 Python 状态和结果超过 16 MiB")
    sys.stdout.write(payload + "\n")
    sys.stdout.flush()


def main():
    """Cache compiled plugin namespaces while the native host retains state."""
    if sys.version_info < (3, 10):
        reply({"ready": False, "error": "需要 Python 3.10 或更新版本"})
        return
    reply({"ready": True, "protocol": PROTOCOL, "version": sys.version.split()[0], "executable": sys.executable})
    namespaces = {}
    while True:
        line = sys.stdin.buffer.readline(MAX_MESSAGE + 1)
        if not line:
            return
        if len(line) > MAX_MESSAGE:
            reply({"ok": False, "error": "Python 请求超过 16 MiB"})
            return
        captured = CapturedLog()
        try:
            request = decode(json.loads(line))
            if request.get("op") == "shutdown":
                return
            if request.get("op") == "drop":
                unload(namespaces, request["node"])
                reply({"ok": True, "dropped": request["node"]})
                continue
            if request.get("op") == "probe":
                modules = {}
                with contextlib.redirect_stdout(captured), contextlib.redirect_stderr(captured):
                    for name in request.get("dependencies", []):
                        try:
                            module = importlib.import_module(name)
                            modules[name] = {"available": True, "version": str(getattr(module, "__version__", ""))}
                        except Exception as error:
                            modules[name] = {"available": False, "error": str(error)}
                reply({"ok": True, "version": sys.version.split()[0], "executable": sys.executable, "modules": modules})
                continue
            node = request["node"]
            if "code" in request:
                with contextlib.redirect_stdout(captured), contextlib.redirect_stderr(captured):
                    for name in request.get("dependencies", []):
                        try:
                            importlib.import_module(name)
                        except ImportError as error:
                            raise RuntimeError("所选 Python 环境缺少依赖：" + name) from error
                    load_namespace(request, namespaces)
            if node not in namespaces:
                raise RuntimeError("Python 插件尚未加载")
            namespace = namespaces[node]["namespace"]
            function = namespace.get(request.get("entry", "process"))
            if not callable(function):
                raise ValueError("Python 插件缺少函数：" + request.get("entry", "process"))
            state = request.get("state", {})
            with contextlib.redirect_stdout(captured), contextlib.redirect_stderr(captured):
                result = function(request.get("inputs", {}), state)
            # Encode both before replying; a failed call never commits its state.
            reply({"ok": True, "result": result, "state": state, "logs": captured.value()})
        except Exception:
            reply({"ok": False, "error": traceback.format_exc(limit=10), "logs": captured.value()})


if __name__ == "__main__":
    # Windows console encodings must not alter the newline-framed UTF-8 protocol.
    sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    main()
