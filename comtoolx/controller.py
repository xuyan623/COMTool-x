"""One command surface for GUI, HTTP API and skill-driven design operations."""

from __future__ import annotations

import copy
import json
import threading
import re
import sys
from pathlib import Path

from PySide6.QtCore import QObject, Signal, Slot

from . import __version__
from .model import new_project, new_id, node_by_id, ui_by_id, update_ui, validate
from .projectio import open_bundle, save_bundle
from .runtime import Engine
from .sdk import WorkbenchError, json_value, checked_properties

COMMANDS = {
    "app.state": "工程/编辑模式/运行状态", "registry": "发现节点、导线、容器及属性和操作",
    "project.get": "读取完整工程模型", "project.new": "name", "project.patch": "name?, metadata?",
    "project.open": "path（.comtoolx）, read_only=false", "project.save": "path?（默认 project/<工程名>.comtoolx）",
    "project.close": "关闭工程并卸载工程插件",
    "project.validate": "检查功能图和 UI", "mode.set": "mode=edit|runtime",
    "graph.node.add": "plugin, id?, name?, position?, rotation=0|90|180|270?, config?",
    "graph.node.patch": "id, name?, config?, position?, rotation=0|90|180|270?",
    "graph.node.move": "id, position=[x,y]", "graph.node.remove": "id",
    "graph.wire.add": "plugin?, id?, source={node,port}, target={node,port}, properties?",
    "graph.wire.patch": "id, properties", "graph.wire.remove": "id",
    "ui.update": "显式同步，保留现有布局", "ui.item.add": "plugin 或 node/view, parent?, title?, stretch?, properties?",
    "ui.item.patch": "id, title?, node?, view?, stretch?, properties?",
    "ui.item.move": "id, parent, index?", "ui.item.remove": "id",
    "batch": "commands=[{op,args}]，一次撤销单位", "history.undo": "撤销",
    "history.redo": "重做", "runtime.start": "验证后运行", "runtime.stop": "停止并释放连接",
    "node.action": "id, action, args={}；返回 job_id",
    "job.get": "id；running/complete/failed", "plugin.load": "path：显式加载本地 Python 插件",
    "capture": "target=window|graph|layout|runtime|对象ID, width?, height?, full?",
    "controls.list": "root?；读取 Qt 控件状态、对象 ID 与可用操作",
    "controls.set": "id, value, submit?；修改标准控件",
    "controls.click": "id；触发按钮/动作", "view.call": "id, action, args={}",
    "view.actions": "读取当前插件视图提供的专用操作", "workspace.set": "view=graph|layout|runtime",
    "graph.fit": "适配功能图视野", "theme.set": "theme=dark|light",
    "graph.overlaps": "读取旋转后的模块占位重叠，不改变布局",
    "library.preview": "plugin；显示插件库模块预览，可截图 library.preview",
    "app.quit": "discard=false；有未保存设计时需先保存或显式 discard",
}


class Controller(QObject):
    """Design transactions are atomic; widgets never maintain a second project model."""

    changed = Signal(str)
    notice = Signal(str, str)

    def __init__(self, registry, data_dir, project_dir=None):
        super().__init__()
        self.registry = registry
        self.baseline = registry.clone()
        self.contents = None
        self.data_dir = Path(data_dir).resolve()
        self.data_dir.mkdir(parents=True, exist_ok=True)
        self.project_dir = Path(project_dir or self.data_dir.parent / "project").resolve()
        self.project_dir.mkdir(parents=True, exist_ok=True)
        self.engine = Engine(registry)
        self.engine.base_dir = str(self.data_dir.parent)
        self.project = new_project()
        self.path = None
        self.design_locked = False
        self.dirty = False
        self.theme = "dark"
        self.undo_stack, self.redo_stack = [], []
        self.window = None
        self.engine.configure(self.project)

    def _activate_project(self, project, registry, path=None, contents=None, read_only=False):
        """Stop old runtimes, rebind the complete design, then release its code/resources."""
        previous = self.contents
        old_modules = self.registry.module_roots - self.baseline.module_roots
        self.engine.stop()
        self.registry = registry
        self.engine.registry = registry
        self.engine.input_files = contents.input_files if contents else {}
        self.engine.base_dir = str(Path(path).parent if path else self.project_dir)
        self.project, self.path, self.contents = project, path, contents
        self.engine.configure(project)
        self.theme = project.get("theme", "dark")
        self.design_locked = read_only
        self.dirty = False
        self.undo_stack.clear()
        self.redo_stack.clear()
        self.changed.emit("project")
        if previous:
            previous.close()
        for name in list(sys.modules):
            if any(name == root or name.startswith(root + ".") for root in old_modules):
                del sys.modules[name]

    def close(self):
        """Release workers before removing project-owned files and module namespaces."""
        self.engine.close()
        if self.contents:
            self.contents.close()
            self.contents = None
        for name in list(sys.modules):
            if any(name == root or name.startswith(root + ".")
                   for root in self.registry.module_roots - self.baseline.module_roots):
                del sys.modules[name]

    def _require_design(self, graph=False):
        if self.design_locked:
            raise WorkbenchError("当前为运行只读模式；请显式进入编辑模式后修改设计")
        if graph and self.engine.running:
            raise WorkbenchError("请先停止功能图，再修改节点或连线")

    def command(self, op, args=None):
        """Execute on the Qt thread; API callers use the queued bridge in api.py."""
        args = args or {}
        if op == "app.state":
            return {"version": __version__, "name": self.project["name"], "path": self.path,
                    "project_dir": str(self.project_dir),
                    "dirty": self.dirty, "mode": "runtime" if self.design_locked else "edit",
                    "theme": self.theme, "runtime": self.engine.snapshot(),
                    "issues": validate(self.project, self.registry),
                    "initialization_errors": self.engine.initialization_errors}
        if op == "registry":
            return self.registry.describe()
        if op == "project.get":
            return copy.deepcopy(self.project)
        if op == "project.validate":
            return {"valid": not validate(self.project, self.registry),
                    "issues": validate(self.project, self.registry)}
        if op in ("project.new", "project.close"):
            self._activate_project(new_project(args.get("name", "未命名工程")), self.baseline.clone())
            return {"name": self.project["name"]}
        if op == "project.open":
            contents = open_bundle(args["path"], self.baseline)
            self._activate_project(contents.project, contents.registry, str(Path(args["path"]).resolve()),
                                   contents, bool(args.get("read_only", False)))
            return {"path": self.path, "mode": "runtime" if self.design_locked else "edit",
                    "issues": validate(self.project, self.registry)}
        if op == "project.save":
            path = args.get("path") or self.path
            if not path:
                name = re.sub(r'[<>:"/\\|?*\x00-\x1f]', "_", self.project["name"]).strip(" .")[:100] or "未命名工程"
                if name.split(".")[0].upper() in {"CON", "PRN", "AUX", "NUL", *[f"COM{i}" for i in range(1, 10)], *[f"LPT{i}" for i in range(1, 10)]}:
                    name = "_" + name
                path = self.project_dir / (name + ".comtoolx")
            self.path = save_bundle(self.project, self.registry, path, self.engine.base_dir, self.engine.input_files)
            self.engine.base_dir = str(Path(self.path).parent)
            self.dirty = False
            self.changed.emit("saved")
            return {"path": self.path}
        if op == "mode.set":
            if args["mode"] not in ("edit", "runtime"):
                raise WorkbenchError("mode 需要 edit 或 runtime")
            self.design_locked = args["mode"] == "runtime"
            self.changed.emit("mode")
            return {"mode": args["mode"]}
        if op == "runtime.start":
            self.engine.start()
            self.changed.emit("runtime")
            return self.engine.snapshot()
        if op == "runtime.stop":
            self.engine.stop()
            self.changed.emit("runtime")
            return self.engine.snapshot()
        if op == "node.action":
            return self.engine.request_action(args["id"], args["action"], args.get("args", {}))
        if op == "job.get":
            return self.engine.job(args["id"])
        if op == "plugin.load":
            self._require_design(graph=True)
            self.registry.load_external(args["path"])
            self.engine.configure(self.project)
            self.changed.emit("plugins")
            return self.registry.describe()
        if op == "theme.set":
            if args["theme"] not in ("dark", "light"):
                raise WorkbenchError("theme 需要 dark 或 light")
            self.theme = args["theme"]
            if not self.design_locked:
                self.project["theme"] = self.theme
                self.dirty = True
            self.changed.emit("theme")
            return {"theme": self.theme}
        if op == "app.quit":
            if self.dirty and not args.get("discard"):
                raise WorkbenchError("工程尚未保存；请先保存或显式指定 discard=true")
            if self.window is not None:
                from PySide6.QtCore import QTimer
                self.window.discard_on_close = bool(args.get("discard"))
                QTimer.singleShot(0, self.window.close)
            return {"closing": True}
        if op in ("capture", "controls.list", "controls.set", "controls.click",
                  "view.call", "view.actions", "workspace.set", "graph.fit", "graph.overlaps",
                  "library.preview"):
            if self.window is None:
                raise WorkbenchError("窗口尚未就绪")
            return self.window.automation(op, args)
        if op in ("history.undo", "history.redo"):
            self._require_design(graph=True)
            source, destination = ((self.undo_stack, self.redo_stack) if op == "history.undo"
                                   else (self.redo_stack, self.undo_stack))
            if not source:
                raise WorkbenchError("没有可撤销/重做的编辑")
            destination.append(copy.deepcopy(self.project))
            self.project = source.pop()
            self.engine.configure(self.project)
            self.dirty = True
            self.changed.emit("graph")
            return {"changed": True}
        operations = args["commands"] if op == "batch" else [{"op": op, "args": args}]
        if not isinstance(operations, list) or len(operations) > 1000:
            raise WorkbenchError("batch 需要最多 1000 个命令")
        graph_changed = any(entry["op"].startswith("graph.") for entry in operations)
        if any(not entry["op"].startswith(("graph.", "ui.", "project.patch")) for entry in operations):
            raise WorkbenchError(f"不支持的编辑命令：{op}")
        self._require_design(graph=graph_changed)
        previous = copy.deepcopy(self.project)
        try:
            results = [self._edit(entry["op"], entry.get("args", {})) for entry in operations]
            # Port errors are rejected at editing time; unresolved UI bindings stay editable.
            issues = validate(self.project, self.registry)
            graph_issues = [issue for issue in issues if not issue["code"].startswith("ui_")]
            structural = [issue for issue in issues if issue["code"] in
                          ("ui_id", "ui_root", "ui_parent", "ui_cycle", "ui_properties")]
            if graph_issues or structural:
                raise WorkbenchError("; ".join(issue["message"] for issue in graph_issues + structural))
        except Exception:
            self.project = previous
            if graph_changed:
                self.engine.configure(previous)
            else:
                self.engine.project = previous
            raise
        if self.project != previous:
            self.undo_stack.append(previous)
            del self.undo_stack[:-100]
            self.redo_stack.clear()
            if graph_changed:
                self.engine.configure(self.project)
            self.dirty = True
            self.changed.emit("graph" if graph_changed else "layout")
        return results if op == "batch" else results[0]

    def _edit(self, op, args):
        """Mutate design data only; IO is deliberately outside the transaction surface."""
        if op == "project.patch":
            if "name" in args:
                self.project["name"] = str(args["name"])
            if "metadata" in args:
                self.project["metadata"].update(copy.deepcopy(args["metadata"]))
            return {"name": self.project["name"], "metadata": self.project["metadata"].copy()}
        if op == "graph.node.add":
            definition = self.registry.nodes.get(args["plugin"])
            if definition is None:
                raise WorkbenchError(f"未安装节点插件：{args['plugin']}")
            node = {"id": args.get("id") or new_id("node"), "plugin": definition.id,
                    "name": args.get("name", definition.name),
                    "rotation": args.get("rotation", 0),
                    "position": args.get("position", [len(self.project["nodes"]) % 3 * 300,
                                                      len(self.project["nodes"]) // 3 * 180]),
                    "config": checked_properties(definition.properties, args.get("config", {}))}
            if any(existing["id"] == node["id"] for existing in self.project["nodes"]):
                raise WorkbenchError("节点 ID 已存在")
            self.project["nodes"].append(node)
            return copy.deepcopy(node)
        if op in ("graph.node.patch", "graph.node.move"):
            node = node_by_id(self.project, args["id"])
            if "name" in args:
                node["name"] = str(args["name"])
            if "position" in args:
                node["position"] = list(args["position"])
            if "rotation" in args:
                node["rotation"] = args["rotation"]
            if "config" in args:
                node["config"] = checked_properties(self.registry.nodes[node["plugin"]].properties,
                                                     args["config"], node["config"])
            return copy.deepcopy(node)
        if op == "graph.node.remove":
            node = node_by_id(self.project, args["id"])
            self.project["nodes"].remove(node)
            self.project["wires"][:] = [wire for wire in self.project["wires"]
                if all(wire[side]["node"] != args["id"] for side in ("source", "target"))]
            return {"removed": args["id"]}
        if op == "graph.wire.add":
            definition = self.registry.wires.get(args.get("plugin", "wire.direct"))
            if definition is None:
                raise WorkbenchError("导线插件不存在")
            properties = {key: copy.deepcopy(spec.get("default"))
                          for key, spec in definition.properties.items()}
            properties.update(args.get("properties", {}))
            wire = {"id": args.get("id") or new_id("wire"), "plugin": definition.id,
                    "source": copy.deepcopy(args["source"]), "target": copy.deepcopy(args["target"]),
                    "properties": properties}
            self.project["wires"].append(wire)
            return copy.deepcopy(wire)
        if op in ("graph.wire.patch", "graph.wire.remove"):
            wire = next((item for item in self.project["wires"] if item["id"] == args["id"]), None)
            if wire is None:
                if op.endswith("remove"):
                    return {"removed": False}
                raise WorkbenchError("导线不存在")
            if op.endswith("remove"):
                self.project["wires"].remove(wire)
                return {"removed": args["id"]}
            wire["properties"].update(args["properties"])
            return copy.deepcopy(wire)
        if op == "ui.update":
            return update_ui(self.project, self.registry)
        if op == "ui.item.add":
            item = {"id": args.get("id") or new_id("ui"), "parent": args.get("parent", self.project["ui"]["root"]),
                    "title": args.get("title", ""), "stretch": int(args.get("stretch", 1)),
                    "properties": copy.deepcopy(args.get("properties", {}))}
            if args.get("plugin"):
                if args["plugin"] not in self.registry.containers:
                    raise WorkbenchError("布局插件不存在")
                item["plugin"] = args["plugin"]
            else:
                node = node_by_id(self.project, args["node"])
                if args["view"] not in self.registry.nodes[node["plugin"]].views:
                    raise WorkbenchError("节点不提供该视图")
                item.update(node=node["id"], view=args["view"])
                self.project["ui"]["hidden"] = [hidden for hidden in self.project["ui"].get("hidden", [])
                    if (hidden["node"], hidden["view"]) != (node["id"], args["view"])]
            self.project["ui"]["items"].append(item)
            return copy.deepcopy(item)
        if op == "ui.item.patch":
            item = ui_by_id(self.project, args["id"])
            for key in ("title", "node", "view", "stretch"):
                if key in args:
                    item[key] = copy.deepcopy(args[key])
            if "properties" in args:
                item.setdefault("properties", {}).update(copy.deepcopy(args["properties"]))
            return copy.deepcopy(item)
        if op == "ui.item.move":
            item = ui_by_id(self.project, args["id"])
            parent = ui_by_id(self.project, args["parent"])
            if item["id"] == self.project["ui"]["root"]:
                raise WorkbenchError("根容器不能移动")
            if not parent.get("plugin"):
                raise WorkbenchError("只能放入布局容器")
            ancestor = parent
            while ancestor is not None:
                if ancestor["id"] == item["id"]:
                    raise WorkbenchError("容器不能放进自己的子级")
                ancestor = (ui_by_id(self.project, ancestor["parent"])
                            if ancestor.get("parent") is not None else None)
            items = self.project["ui"]["items"]
            items.remove(item)
            item["parent"] = parent["id"]
            siblings = [entry for entry in items if entry.get("parent") == parent["id"]]
            index = int(args.get("index", len(siblings)))
            reference = siblings[max(0, index)] if 0 <= index < len(siblings) else None
            items.insert(items.index(reference), item) if reference else items.append(item)
            return copy.deepcopy(item)
        if op == "ui.item.remove":
            if args["id"] == self.project["ui"]["root"]:
                raise WorkbenchError("根容器不能删除")
            ui_by_id(self.project, args["id"])
            removed = {args["id"]}
            items = self.project["ui"]["items"]
            while True:
                children = {item["id"] for item in items if item.get("parent") in removed}
                if children <= removed:
                    break
                removed.update(children)
            hidden = self.project["ui"].setdefault("hidden", [])
            hidden.extend({"node": item["node"], "view": item["view"]}
                          for item in items if item["id"] in removed and item.get("node"))
            items[:] = [item for item in items if item["id"] not in removed]
            return {"removed": sorted(removed)}
        raise WorkbenchError(f"未知命令：{op}")
