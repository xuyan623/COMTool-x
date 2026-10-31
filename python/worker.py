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
import os
import threading
import time
import uuid
import copy
from collections.abc import Mapping
from pathlib import Path

PROTOCOL = 3
MAX_MESSAGE = int(os.environ.get("CORIG_PYTHON_MESSAGE_BYTES",16 * 1024 * 1024))
_protocol_output = sys.stdout
_write_lock = threading.Lock()
_capture = threading.local()
_tasks = {}
_task_lock = threading.RLock()
_shared = {}


class LogStream:
    """Route print per thread without redirecting another task's stdout."""
    def __init__(self, fallback):
        self.fallback = fallback

    def write(self, text):
        target = getattr(_capture, "stream", None)
        return (target if target is not None else self.fallback).write(text)

    def flush(self):
        target = getattr(_capture, "stream", None)
        (target if target is not None else self.fallback).flush()


@contextlib.contextmanager
def capture_logs(stream):
    """Restore the calling thread's capture after nested plugin operations."""
    previous = getattr(_capture, "stream", None)
    _capture.stream = stream
    try:
        yield
    finally:
        _capture.stream = previous


class PackageResources(Mapping):
    """Read checked materialized resources lazily, outside the JSON channel."""
    def __init__(self, root):
        self.root = root
        self.names = tuple(p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file())

    def __iter__(self):
        return iter(self.names)

    def __len__(self):
        return len(self.names)

    def __getitem__(self, name):
        path = (self.root / name).resolve()
        if not path.is_relative_to(self.root) or name not in self.names:
            raise KeyError(name)
        return path.read_bytes()


class TaskCancelled(Exception):
    """Cooperative cancellation never commits an unfinished state change."""


class ServiceContext:
    """Public producer/task interface; all output returns through the engine."""
    def __init__(self, node, record, task=None, pause_sensitive=True):
        self.node, self.record, self.task = node, record, task
        self.pause_sensitive = pause_sensitive
        self.shared = _shared

    @property
    def config(self):
        return copy.deepcopy(self.record.get("config", {}))

    def cancelled(self):
        return self.record["stopped"].is_set() or (self.task is not None and self.task["cancel"].is_set())

    def checkpoint(self):
        while self.pause_sensitive and not self.record["resumed"].wait(.02):
            if self.cancelled():
                raise TaskCancelled("任务已取消")
        if self.cancelled():
            raise TaskCancelled("任务已取消")

    def sleep(self, seconds):
        remaining = max(0., float(seconds))
        while remaining > 0:
            self.checkpoint()
            start = time.monotonic()
            time.sleep(min(.02, remaining))
            remaining -= time.monotonic() - start
        self.checkpoint()

    def publish(self, outputs):
        if self.record.get("metadata"):
            raise RuntimeError("声明校验不能发布运行数据")
        self.checkpoint()
        reply({"event":"outputs", "node":self.node, "outputs":outputs})

    def observe(self, state):
        if self.record.get("metadata"):
            raise RuntimeError("声明校验不能修改运行状态")
        self.checkpoint()
        if not isinstance(state,dict) or "script_state" in state:
            raise ValueError("observe 需要状态对象；script_state 由节点状态提交管理")
        encode(state)
        with self.record["lock"]:
            self.record["observed"].update(copy.deepcopy(state))
            snapshot = copy.deepcopy(self.record["observed"])
        reply({"event":"state", "node":self.node, "state":snapshot})

    def progress(self, value, message=""):
        self.checkpoint()
        if self.task is None:
            raise RuntimeError("progress 需要后台任务上下文")
        number = float(value)
        if not math.isfinite(number) or not 0 <= number <= 1:
            raise ValueError("任务进度需要 0～1")
        reply({"event":"task", "task":self.task["id"], "node":self.node,
               "state":"running", "progress":number, "message":str(message)})

    def background(self, function, *arguments, name="background"):
        """Start a producer owned by this node; lifecycle hooks control it."""
        if self.record.get("metadata"):
            raise RuntimeError("声明校验不能启动后台任务")
        return start_task(self.node,self.record,function,arguments,name,True,False)


def commit_state(node, record, before, after):
    """Merge independent state fields and reject concurrent writes to one key."""
    absent = object()
    changed = [key for key in before.keys() | after.keys() if before.get(key,absent) != after.get(key,absent)]
    encode(after)
    with record["lock"]:
        current = record["state"]
        for key in changed:
            if current.get(key,absent) != before.get(key,absent):
                raise RuntimeError("节点状态同时发生修改："+str(key))
        for key in changed:
            if key in after:
                current[key] = after[key]
            else:
                current.pop(key,None)
        snapshot = copy.deepcopy(current)
        record["observed"]["script_state"] = snapshot
        observed = copy.deepcopy(record["observed"])
    reply({"event":"state", "node":node, "state":observed})
    return snapshot


def start_task(node, record, function, arguments, name, pause_sensitive, action):
    """Run long actions off the request thread and report actual completion."""
    task = {"id":"python."+uuid.uuid4().hex, "node":node, "record":record, "cancel":threading.Event()}
    def run():
        captured = CapturedLog()
        context = ServiceContext(node,record,task,pause_sensitive)
        reply({"event":"task", "task":task["id"], "node":node, "state":"running", "progress":0., "name":name})
        try:
            context.checkpoint()
            with capture_logs(captured):
                if action:
                    with record["lock"]:
                        before = copy.deepcopy(record["state"])
                    after = copy.deepcopy(before)
                    result = function(arguments[0],after,context)
                    context.checkpoint()
                    # Reject an unsavable/oversized result before publishing
                    # any new script state. Cancellation ends at this commit.
                    serialize_message({"result":result,"state":after,"logs":captured.value()})
                    with _task_lock:
                        if context.cancelled():
                            raise TaskCancelled("任务已取消")
                        task["committing"] = True
                    committed = commit_state(node,record,before,after)
                else:
                    result = function(context,*arguments)
                    committed = None
            if not action:
                context.checkpoint()
            encode(result)
            if action and isinstance(result,dict) and result.get("outputs"):
                reply({"event":"outputs","node":node,"outputs":result["outputs"]})
            reply({"event":"task", "task":task["id"], "node":node, "state":"complete", "progress":1.,
                   "result":result, "script_state":committed, "logs":captured.value()})
        except TaskCancelled as error:
            reply({"event":"task", "task":task["id"], "node":node, "state":"cancelled", "error":str(error), "logs":captured.value()})
        except Exception:
            reply({"event":"task", "task":task["id"], "node":node, "state":"failed",
                   "error":traceback.format_exc(limit=10), "logs":captured.value()})
        finally:
            task["done"] = True
    task["thread"] = threading.Thread(target=run,name="corig-"+name,daemon=True)
    with _task_lock:
        if len(_tasks) > 512:
            for key in list(_tasks):
                if _tasks[key].get("done"):
                    del _tasks[key]
        _tasks[task["id"]] = task
    task["thread"].start()
    return task["id"]


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


def unload(namespaces, key):
    """Remove only this plugin's temporary package and imported submodules."""
    old = namespaces.pop(key, None)
    if old is not None:
        old["stopped"].set(); old["resumed"].set()
        with _task_lock:
            tasks = [task for task in _tasks.values() if task["record"] is old and not task.get("done")]
        for task in tasks:
            task["cancel"].set(); task["thread"].join(.25)
            if task["thread"].is_alive():
                namespaces[key] = old
                raise RuntimeError("节点仍有未响应取消的后台任务，请重启此插件工作进程")
        prefix = old["package"]
        for name in list(sys.modules):
            if name == prefix or name.startswith(prefix + "."):
                del sys.modules[name]
        if old["directory"] is not None:
            old["directory"].cleanup()


def load_namespace(request, namespaces):
    """Expand portable resources into an isolated package for relative imports."""
    node = request["node"]
    key = ("metadata" if request.get("op") == "metadata" else "runtime", node)
    unload(namespaces, key)
    package = "corig_ext_" + hashlib.sha256(repr(key).encode()).hexdigest()[:16]
    external = request.get("directory")
    directory = None if external else tempfile.TemporaryDirectory(prefix="corig-python-")
    root = Path(external if external else directory.name).resolve()
    try:
        for name, content in request.get("resources", {}).items():
            relative = Path(name)
            target = (root / relative).resolve()
            if relative.is_absolute() or not target.is_relative_to(root):
                raise ValueError("插件资源不能越出所在目录：" + name)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content if isinstance(content, bytes) else str(content).encode("utf-8"))
        if external:
            source = (root / request["code_file"]).resolve()
            if not source.is_relative_to(root):
                raise ValueError("Python 代码必须位于插件目录内")
            code = source.read_text("utf-8")
        else:
            source = root / "__init__.py"
            code = request["code"]
            source.write_text(code,encoding="utf-8")
        specification = importlib.util.spec_from_file_location(package, source, submodule_search_locations=[str(root)])
        module = importlib.util.module_from_spec(specification)
        namespace = module.__dict__
        namespace.update({"__package__": package, "__path__": [str(root)], "__file__": str(source),
                          "math": math, "json": json, "struct": struct, "crc16": crc16,
                          "resources": PackageResources(root) if external else request.get("resources", {})})
        sys.modules[package] = module
        record = {"package":package, "directory":directory, "namespace":namespace, "node":node,
                  "metadata":request.get("op")=="metadata",
                  "state":copy.deepcopy(request.get("state",{})), "lock":threading.RLock(),
                  "stopped":threading.Event(), "resumed":threading.Event(),
                  "config":copy.deepcopy(request.get("inputs",{}).get("__config__",{}))}
        record["observed"] = {"script_state":copy.deepcopy(record["state"])}
        record["resumed"].set()
        namespaces[key] = record
        namespace["service"] = ServiceContext(node,record)
        exec(compile(code,str(source),"exec"),namespace)
        return namespace
    except Exception:
        unload(namespaces, key)
        if directory is not None:
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


def serialize_message(value):
    """Check portability and the configured wire budget before state commit."""
    payload = json.dumps(encode(value), ensure_ascii=False, allow_nan=False, separators=(",", ":"))
    if len(payload.encode("utf-8")) > MAX_MESSAGE:
        raise ValueError("单次 Python 状态和结果超过 python_message_mib 额度")
    return payload


def reply(value):
    """Keep protocol output separate from plugin print/log output."""
    payload = serialize_message(value)
    with _write_lock:
        _protocol_output.write(payload + "\n")
        _protocol_output.flush()


def main():
    """Cache compiled plugin namespaces while the native host retains state."""
    if sys.version_info < (3, 10):
        reply({"ready": False, "error": "需要 Python 3.10 或更新版本"})
        return
    reply({"ready": True, "protocol": PROTOCOL, "version": sys.version.split()[0], "executable": sys.executable})
    sys.stdout, sys.stderr = LogStream(sys.stderr), LogStream(sys.stderr)
    namespaces = {}
    while True:
        line = sys.stdin.buffer.readline(MAX_MESSAGE + 1)
        if not line:
            return
        if len(line) > MAX_MESSAGE:
            reply({"ok": False, "error": "Python 请求超过 16 MiB"})
            return
        captured = CapturedLog()
        request = {}
        try:
            request = decode(json.loads(line))
            if request.get("op") == "shutdown":
                return
            if request.get("op") == "drop":
                unload(namespaces, ("runtime",request["node"]))
                reply({"ok": True, "dropped": request["node"],"request":request.get("request")})
                continue
            if request.get("op") == "task.cancel":
                with _task_lock:
                    task = _tasks.get(request["task"])
                    if task is None:
                        raise ValueError("后台任务不存在")
                    if not task.get("committing") and not task.get("done"):
                        task["cancel"].set()
                reply({"ok":True,"request":request.get("request"),"requested":True})
                continue
            if request.get("op") == "probe":
                modules = {}
                with capture_logs(captured):
                    for name in (request.get("dependencies") or []):
                        try:
                            module = importlib.import_module(name)
                            modules[name] = {"available": True, "version": str(getattr(module, "__version__", ""))}
                        except Exception as error:
                            modules[name] = {"available": False, "error": str(error)}
                reply({"ok": True, "version": sys.version.split()[0], "executable": sys.executable, "modules": modules,"request":request.get("request")})
                continue
            node = request["node"]
            namespace_key = ("metadata" if request.get("op") == "metadata" else "runtime",node)
            if "code" in request or "code_file" in request:
                with capture_logs(captured):
                    for name in (request.get("dependencies") or []):
                        try:
                            importlib.import_module(name)
                        except ImportError as error:
                            raise RuntimeError("所选 Python 环境缺少依赖：" + name) from error
                    load_namespace(request, namespaces)
            if namespace_key not in namespaces:
                raise RuntimeError("Python 插件尚未加载")
            namespace = namespaces[namespace_key]["namespace"]
            record = namespaces[namespace_key]
            record["config"] = copy.deepcopy(request.get("inputs",{}).get("__config__",record["config"]))
            if request.get("op") == "configuration":
                reply({"ok":True,"request":request.get("request"),"state":record["state"],"logs":captured.value()})
                continue
            if request.get("op") == "lifecycle":
                phase = request["phase"]
                if phase == "start":
                    record["stopped"].clear(); record["resumed"].clear()
                if phase == "resume":
                    record["resumed"].set()
                if phase == "pause":
                    record["resumed"].clear()
                if phase == "stop":
                    record["stopped"].set(); record["resumed"].set()
                hook = namespace.get("on_"+phase)
                if callable(hook):
                    with capture_logs(captured):
                        hook(namespace["service"])
                reply({"ok":True,"request":request.get("request"),"state":record["state"],"logs":captured.value()})
                continue
            function = namespace.get(request.get("entry", "process"))
            if not callable(function):
                raise ValueError("Python 插件缺少函数：" + request.get("entry", "process"))
            if request.get("op") == "task.start":
                task = start_task(node,record,function,(copy.deepcopy(request.get("inputs",{})),),
                                  request.get("entry","action"),request.get("pause_sensitive",True),True)
                reply({"ok":True,"request":request.get("request"),"result":{"python_task":task},"state":record["state"],"logs":captured.value()})
                continue
            with record["lock"]:
                before = copy.deepcopy(record["state"])
            state = copy.deepcopy(before)
            with capture_logs(captured):
                if request.get("op") == "metadata":
                    result = function(request.get("config",{}),request.get("context",{}))
                else:
                    result = function(request.get("inputs", {}), state)
            serialize_message({"ok":True,"result":result,"state":state,"logs":captured.value(),"request":request.get("request")})
            committed = record["state"] if request.get("op") == "metadata" else commit_state(node,record,before,state)
            reply({"ok": True, "result": result, "state": committed, "logs": captured.value(),"request":request.get("request")})
        except Exception:
            reply({"ok": False, "error": traceback.format_exc(limit=10), "logs": captured.value(),"request":request.get("request")})


if __name__ == "__main__":
    # Windows console encodings must not alter the newline-framed UTF-8 protocol.
    sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    main()
