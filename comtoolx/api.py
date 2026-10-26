"""Authenticated loopback HTTP commands and a Qt-thread dispatch bridge."""

from __future__ import annotations

import concurrent.futures
import hmac
import json
import logging
import os
import re
import secrets
import threading
import tempfile
import time
from contextlib import contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse
from urllib.request import Request, urlopen
from urllib.error import HTTPError

from PySide6.QtCore import QObject, Signal, Slot, Qt, QThread, QTimer, QLockFile

from . import __version__
from .controller import COMMANDS
from .sdk import WorkbenchError, json_value


def _read_discovery(path):
    """Treat missing, interrupted or malformed discovery data as unavailable."""
    try:
        info = json.loads(Path(path).read_text(encoding="utf-8"))
        return info if isinstance(info, dict) else None
    except (OSError, ValueError):
        return None


def _live_discovery(info):
    """Authenticate the recorded loopback server without waiting for its Qt thread."""
    if (not isinstance(info, dict) or not isinstance(info.get("token"), str)
            or not re.fullmatch(r"http://127\.0\.0\.1:[0-9]{1,5}", str(info.get("url", "")))):
        return False
    try:
        request = Request(info["url"] + "/v1/schema", headers={"Authorization": "Bearer " + info["token"]})
        with urlopen(request, timeout=0.3) as response:
            return response.status == 200
    except HTTPError as exc:
        exc.close()
        return False
    except (OSError, ValueError):
        return False


@contextmanager
def _discovery_lock(directory):
    """Serialize publication, failover and removal across independent GUI processes."""
    lock = QLockFile(str(Path(directory) / ".api-discovery.lock"))
    if not lock.tryLock(3000):
        raise WorkbenchError("API 连接信息正在更新，请稍后重试")
    try:
        yield
    finally:
        lock.unlock()


def _write_discovery(path, info):
    """Publish complete JSON atomically so concurrent readers never see half a record."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + ".", suffix=".tmp", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(json.dumps(info, ensure_ascii=False, indent=2).encode("utf-8"))
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def _surviving_discovery(directory):
    """Choose the most recently started authenticated survivor, ignoring crash leftovers."""
    candidates = [_read_discovery(path) for path in (Path(directory) / "instances").glob("api-*.json")]
    candidates = [info for info in candidates if info is not None]
    candidates.sort(key=lambda info: info.get("started_at", 0)
                    if isinstance(info.get("started_at", 0), (int, float)) else 0, reverse=True)
    return next((info for info in candidates if _live_discovery(info)), None)


def discover_api(directory):
    """Resolve a live window and repair the shared entry when its previous owner disappeared."""
    directory = Path(directory)
    alias = directory / "api.json"
    info = _read_discovery(alias)
    if _live_discovery(info):
        return info
    if directory.is_dir():
        with _discovery_lock(directory):
            info = _read_discovery(alias)
            if _live_discovery(info):
                return info
            info = _surviving_discovery(directory)
            if info is not None:
                _write_discovery(alias, info)
                return info
    raise WorkbenchError("未找到可连接的 COMTool-x 窗口，请启动软件后重试")


class Bridge(QObject):
    """HTTP workers wait for real GUI-thread results, never touch Qt widgets directly."""

    submitted = Signal(object)

    def __init__(self, controller):
        super().__init__()
        self.controller = controller
        self.submitted.connect(self._execute, Qt.ConnectionType.QueuedConnection)

    @Slot(object)
    def _execute(self, ticket):
        future, op, args = ticket
        if not future.set_running_or_notify_cancel():
            return
        try:
            future.set_result(json_value(self.controller.command(op, args)))
        except Exception as exc:
            future.set_exception(exc)

    def call(self, op, args):
        if QThread.currentThread() is self.thread():
            return json_value(self.controller.command(op, args))
        future = concurrent.futures.Future()
        self.submitted.emit((future, op, args))
        try:
            return future.result(timeout=20)
        except concurrent.futures.TimeoutError:
            future.cancel()
            raise WorkbenchError("Qt 操作超时；请查询工程状态后再决定是否重试")


class ApiServer:
    """Expose only the chosen application on 127.0.0.1 with a per-launch token."""

    def __init__(self, controller, port=0):
        self.controller = controller
        self.bridge = Bridge(controller)
        self.token = secrets.token_urlsafe(32)
        outer = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, fmt, *args):
                # Design traffic is observable through errors and results, not noisy access logs.
                return

            def reply(self, code, data, content_type="application/json; charset=utf-8"):
                payload = (json.dumps(json_value(data), ensure_ascii=False, allow_nan=False).encode("utf-8")
                           if content_type.startswith("application/json") else data)
                self.send_response(code)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(payload)))
                self.send_header("Cache-Control", "no-store")
                self.send_header("X-Content-Type-Options", "nosniff")
                self.end_headers()
                try:
                    self.wfile.write(payload)
                except (BrokenPipeError, ConnectionResetError):
                    return

            def authorized(self):
                expected = "Bearer " + outer.token
                if not hmac.compare_digest(self.headers.get("Authorization", ""), expected):
                    self.reply(401, {"ok": False, "error": "需要本次启动的本地 API token"})
                    return False
                return True

            def do_GET(self):
                path = urlparse(self.path).path
                if path == "/v1/health":
                    self.reply(200, {"ok": True, "version": __version__})
                    return
                if not self.authorized():
                    return
                try:
                    routes = {"/v1/state": "app.state", "/v1/project": "project.get",
                              "/v1/plugins": "registry"}
                    if path == "/v1/schema":
                        self.reply(200, {"ok": True, "commands": COMMANDS,
                            "request": {"op": "command name", "args": {}},
                            "response": {"ok": True, "result": "operation result"}})
                    elif path in routes:
                        self.reply(200, {"ok": True, "result": outer.bridge.call(routes[path], {})})
                    elif path.startswith("/v1/jobs/"):
                        result = outer.bridge.call("job.get", {"id": path.rsplit("/", 1)[-1]})
                        self.reply(200, {"ok": True, "result": result})
                    elif path.startswith("/v1/screenshots/"):
                        filename = path.rsplit("/", 1)[-1]
                        if not re.fullmatch(r"[0-9a-f]{32}\.png", filename):
                            raise WorkbenchError("截图 ID 无效")
                        target = controller.data_dir / "screenshots" / filename
                        self.reply(200, target.read_bytes(), "image/png")
                    else:
                        self.reply(404, {"ok": False, "error": "接口不存在"})
                except (ValueError, KeyError, FileNotFoundError) as exc:
                    self.reply(400, {"ok": False, "error": str(exc)})
                except Exception as exc:
                    controller.engine.record("error", f"API：{exc}")
                    self.reply(500, {"ok": False, "error": str(exc)})

            def do_POST(self):
                if not self.authorized():
                    return
                if urlparse(self.path).path != "/v1/command":
                    self.reply(404, {"ok": False, "error": "接口不存在"})
                    return
                try:
                    size = int(self.headers.get("Content-Length", "0"))
                    if not 0 < size <= 16 * 1024 * 1024:
                        raise WorkbenchError("请求体需要在 1 B～16 MiB 之间")
                    if not self.headers.get("Content-Type", "").startswith("application/json"):
                        raise WorkbenchError("请求需要 application/json")
                    payload = json.loads(self.rfile.read(size).decode("utf-8"))
                    if payload.get("op") not in COMMANDS:
                        raise WorkbenchError("命令不存在；请读取 /v1/schema")
                    args = payload.get("args", {})
                    if not isinstance(args, dict):
                        raise WorkbenchError("args 必须是对象")
                    result = outer.bridge.call(payload["op"], args)
                    self.reply(200, {"ok": True, "result": result})
                except (ValueError, KeyError, TypeError) as exc:
                    self.reply(400, {"ok": False, "error": str(exc)})
                except Exception as exc:
                    controller.engine.record("error", f"API：{exc}")
                    self.reply(500, {"ok": False, "error": str(exc)})

        self.http = ThreadingHTTPServer(("127.0.0.1", port), Handler)
        self.http.daemon_threads = True
        self.thread = threading.Thread(target=self.http.serve_forever, daemon=True, name="local-api")
        self.info = {"url": f"http://127.0.0.1:{self.http.server_port}",
                     "token": self.token, "pid": os.getpid(), "parent_pid": os.getppid(),
                     "version": __version__,
                     "data_dir": str(controller.data_dir), "started_at": time.time()}
        self.info_path = controller.data_dir / "api.json"
        self.instance_path = controller.data_dir / "instances" / f"api-{os.getpid()}-{self.http.server_port}.json"
        self.closed = False
        self.discovery_timer = QTimer(self.bridge)
        self.discovery_timer.setInterval(2000)
        self.discovery_timer.timeout.connect(self.repair_discovery)

    def start(self):
        """Start accepting requests before publishing this window's independent record."""
        self.thread.start()
        try:
            with _discovery_lock(self.controller.data_dir):
                _write_discovery(self.instance_path, self.info)
                _write_discovery(self.info_path, self.info)
            self.discovery_timer.start()
        except Exception:
            self.close()
            raise

    def repair_discovery(self):
        """Recover removed records or a crashed default owner without taking over a live peer."""
        if self.closed:
            return
        try:
            with _discovery_lock(self.controller.data_dir):
                if _read_discovery(self.instance_path) != self.info:
                    _write_discovery(self.instance_path, self.info)
                if not _live_discovery(_read_discovery(self.info_path)):
                    _write_discovery(self.info_path, _surviving_discovery(self.controller.data_dir) or self.info)
        except (OSError, WorkbenchError) as exc:
            logging.warning("Unable to restore API discovery: %s", exc)

    def close(self):
        """Remove only our exact record; keep the shared entry attached to a surviving window."""
        if self.closed:
            return
        self.closed = True
        self.discovery_timer.stop()
        self.http.shutdown()
        self.http.server_close()
        try:
            with _discovery_lock(self.controller.data_dir):
                if _read_discovery(self.instance_path) == self.info:
                    self.instance_path.unlink(missing_ok=True)
                if not _live_discovery(_read_discovery(self.info_path)):
                    survivor = _surviving_discovery(self.controller.data_dir)
                    if survivor is not None:
                        _write_discovery(self.info_path, survivor)
                    else:
                        self.info_path.unlink(missing_ok=True)
        except (OSError, WorkbenchError) as exc:
            logging.warning("Unable to update API discovery on close: %s", exc)
