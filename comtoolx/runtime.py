"""Threaded graph execution with typed one-to-one routing and observable failures."""

from __future__ import annotations

import concurrent.futures
# COMPAT BEGIN: PySide6 6.12 on CPython 3.15 leaves the futures lazy attribute
# unresolved after its import hook is installed. A concrete module import works.
# Scope: executor construction only. Remove after that dependency combination
# resolves concurrent.futures.ThreadPoolExecutor to an actual class.
from concurrent.futures.thread import ThreadPoolExecutor
# COMPAT END
import copy
import queue
import threading
import time
import uuid
from collections import deque

from .model import validate
from .sdk import Registry, WorkbenchError, json_value


class Engine:
    """Keep device IO, processing and graph data off the Qt drawing thread."""

    def __init__(self, registry: Registry):
        self.registry = registry
        self.project = None
        self.nodes = {}
        self.routes = {}
        self.running = False
        self.lock = threading.RLock()
        self.events = deque(maxlen=500)
        self.edge_counts = {}
        self.inbox = queue.Queue(maxsize=32768)
        self.worker = None
        # Preserve action order, including fast terminal keystrokes and file sends.
        # Device RX/TX and graph processing have their own independent workers.
        self.actions = ThreadPoolExecutor(max_workers=1,
                                                             thread_name_prefix="plugin-action")
        self.jobs = {}
        self.processed = 0
        self.failed = 0
        self.base_dir = "."
        self.input_files = {}
        self.initialization_errors = {}

    def configure(self, project: dict):
        """Build disconnected plugin instances while the graph is stopped."""
        if self.running:
            raise WorkbenchError("请先停止功能图，再修改节点或连线")
        with self.lock:
            for runtime in self.nodes.values():
                runtime.stop()
            self.project = project
            self.nodes = {}
            self.initialization_errors = {}
            for node in project["nodes"]:
                definition = self.registry.nodes.get(node["plugin"])
                if definition is not None:
                    try:
                        runtime_node = copy.deepcopy(node)
                        for key, schema in definition.properties.items():
                            resource = self.input_files.get((node["id"], key))
                            if (schema.get("format") == "file" and resource
                                    and node.get("config", {}).get(key, schema.get("default")) == resource["value"]):
                                runtime_node.setdefault("config", {})[key] = resource["path"]
                        self.nodes[node["id"]] = definition.factory(runtime_node, self)
                    except Exception as exc:
                        self.initialization_errors[node["id"]] = str(exc)
                        self.record("error", f"初始化 {node['id']}：{exc}")
            self.routes = {(w["source"]["node"], w["source"]["port"]):
                           (w["target"]["node"], w["target"]["port"], w["id"])
                           for w in project["wires"]}
            self.edge_counts = {w["id"]: 0 for w in project["wires"]}
            self.inbox = queue.Queue(maxsize=32768)

    def start(self):
        """A graph starts processing only after validation and explicit user/API intent."""
        if self.running:
            return
        issues = validate(self.project, self.registry)
        # An unfinished UI is editable, but invalid graph structure must not execute.
        blocking = [issue for issue in issues if not issue["code"].startswith("ui_")]
        if blocking:
            raise WorkbenchError("; ".join(issue["message"] for issue in blocking))
        if self.initialization_errors:
            raise WorkbenchError(f"插件初始化失败：{self.initialization_errors}")
        self.running = True
        try:
            for runtime in self.nodes.values():
                runtime.start()
        except Exception:
            self.running = False
            for runtime in self.nodes.values():
                runtime.stop()
            raise
        self.worker = threading.Thread(target=self._run, name="graph-dataflow", daemon=True)
        self.worker.start()
        self.record("info", "功能图已运行")

    def stop(self):
        """Release IO and scheduled tasks; no connection survives a graph stop."""
        self.running = False
        for runtime in tuple(self.nodes.values()):
            try:
                runtime.stop()
            except Exception as exc:
                self.record("error", f"停止 {runtime.id}：{exc}")
        if self.worker and self.worker is not threading.current_thread():
            self.worker.join(timeout=2)
        self.worker = None
        while True:
            try:
                *_, receipt = self.inbox.get_nowait()
                if not receipt.done():
                    receipt.set_exception(WorkbenchError("功能图已停止"))
                self.inbox.task_done()
            except queue.Empty:
                break

    def publish(self, source: str, port: str, data):
        """Queue immutable payload references; serialization is only an API concern."""
        if not self.running:
            raise WorkbenchError("功能图未运行")
        receipt = concurrent.futures.Future()
        try:
            self.inbox.put_nowait((source, port, data, receipt))
        except queue.Full as exc:
            self.failed += 1
            self.record("error", "数据队列已满；请降低输入速率或优化处理插件")
            raise WorkbenchError("数据队列已满，当前数据未入队") from exc
        return receipt

    def _run(self):
        """A single dispatcher preserves arrival order without recursive data callbacks."""
        while self.running:
            try:
                source, port, data, receipt = self.inbox.get(timeout=0.05)
            except queue.Empty:
                continue
            try:
                route = self.routes.get((source, port))
                if route is not None:
                    target, target_port, edge = route
                    runtime = self.nodes[target]
                    with runtime.lock:
                        runtime.state["received"] += 1
                    downstream = runtime.receive(target_port, data)
                    self.edge_counts[edge] += 1
                    if isinstance(downstream, concurrent.futures.Future):
                        def finish(completed, destination=receipt, delivered_node=target):
                            if not destination.done():
                                try:
                                    value = completed.result()
                                    if isinstance(value, dict):
                                        destination.set_result({**value,
                                            "delivered": value.get("delivered", True),
                                            "route": [delivered_node, *value.get("route", [])]})
                                    else:
                                        destination.set_result({"delivered": True, "node": delivered_node,
                                                                "result": json_value(value)})
                                except Exception as error:
                                    destination.set_exception(error)
                        downstream.add_done_callback(finish)
                    else:
                        receipt.set_result({"delivered": True, "node": target,
                                            "result": json_value(downstream), "route": [target]})
                else:
                    receipt.set_result({"delivered": False, "reason": "unconnected"})
                self.processed += 1
            except Exception as exc:
                self.failed += 1
                self.record("error", f"{source}.{port}：{exc}")
                if not receipt.done():
                    receipt.set_exception(exc)
            finally:
                self.inbox.task_done()

    def record(self, level: str, message: str):
        """Retain bounded, timestamped diagnostics for the shell and external clients."""
        with self.lock:
            self.events.append({"time": time.time(), "level": level, "message": str(message)})

    def request_action(self, node_id: str, name: str, args: dict | None = None) -> dict:
        """Run potentially blocking IO outside Qt and expose its real completion result."""
        if node_id not in self.nodes:
            raise WorkbenchError(f"节点不存在：{node_id}")
        runtime = self.nodes[node_id]
        definition = self.registry.nodes[runtime.node["plugin"]]
        if name not in definition.actions:
            raise WorkbenchError(f"{definition.name} 没有操作 {name}")
        requires_running = definition.actions[name].get("requires_running", True)
        if requires_running and not self.running:
            raise WorkbenchError("请先运行功能图，再操作设备或控件")
        ident = uuid.uuid4().hex[:12]
        def perform():
            try:
                if self.nodes.get(node_id) is not runtime:
                    raise WorkbenchError("节点已被替换，请重新发现节点状态")
                if requires_running and not self.running:
                    raise WorkbenchError("功能图已停止，操作已取消")
                result = runtime.action(name, args or {})
                return json_value(result)
            except Exception as exc:
                with runtime.lock:
                    runtime.state["error"] = str(exc)
                self.record("error", f"{runtime.node['name']} / {name}：{exc}")
                raise
        if definition.actions[name].get("immediate"):
            # Only declared nonblocking state operations use this path.
            future = concurrent.futures.Future()
            try:
                future.set_result(perform())
            except Exception as exc:
                future.set_exception(exc)
        else:
            future = self.actions.submit(perform)
        with self.lock:
            self.jobs[ident] = {"future": future, "node": node_id, "action": name,
                               "created": time.time()}
            # Retain recent results; pending operations are never discarded.
            for key in list(self.jobs)[:-200]:
                if self.jobs[key]["future"].done():
                    del self.jobs[key]
        return {"job_id": ident}

    def job(self, ident: str) -> dict:
        """Distinguish acceptance, successful completion and an actual plugin failure."""
        with self.lock:
            item = self.jobs.get(ident)
        if item is None:
            raise WorkbenchError(f"任务不存在或已过期：{ident}")
        result = {key: value for key, value in item.items() if key != "future"}
        future = item["future"]
        if not future.done():
            return {**result, "status": "running"}
        try:
            return {**result, "status": "complete", "result": future.result()}
        except Exception as exc:
            return {**result, "status": "failed", "error": str(exc)}

    def snapshot(self) -> dict:
        return {"running": self.running, "queued": self.inbox.qsize(),
                "processed": self.processed, "failed": self.failed,
                "wires": self.edge_counts.copy(),
                "nodes": {key: json_value(runtime.snapshot())
                          for key, runtime in tuple(self.nodes.items())},
                "events": list(self.events)}

    def close(self):
        """Application shutdown also owns the lifetime of API-initiated operations."""
        self.stop()
        self.actions.shutdown(wait=True, cancel_futures=True)
