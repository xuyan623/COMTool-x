"""Plugin contracts shared by the graph, layout editor, runtime and local API.

Plugins own domain behaviour. The shell only understands declared ports,
properties, views and actions; it contains no serial or plotting special cases.
"""

from __future__ import annotations

import copy
import ast
import importlib.util
import math
import re
import threading
import sys
import types
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable


class WorkbenchError(ValueError):
    """An actionable operation error suitable for GUI and API callers."""


UI_PROPERTIES = {
    **{key: {"type": "integer", "min": 0, "max": 16777215}
       for key in ("min_width", "min_height", "max_width", "max_height")},
    **{key: {"type": "integer", "min": 0, "max": 1000}
       for key in ("row", "column")},
    **{key: {"type": "integer", "min": 1, "max": 1000}
       for key in ("rowspan", "colspan")},
    "font_size": {"type": "integer", "min": 6, "max": 96},
    "font_family": {"type": "string"},
    "color": {"type": "string", "pattern": "#[0-9a-fA-F]{6}"},
    "background": {"type": "string", "pattern": "#[0-9a-fA-F]{6}"},
    "label": {"type": "string"}, "text": {"type": "string"},
}


def checked_properties(schema: dict, changes: dict, previous: dict | None = None) -> dict:
    """Validate a declared node, wire or view contract before committing design data."""
    if not isinstance(changes, dict):
        raise WorkbenchError("属性必须是对象")
    unknown = set(changes) - schema.keys()
    if unknown:
        raise WorkbenchError(f"不支持属性：{', '.join(sorted(unknown))}")
    result = copy.deepcopy(previous if previous is not None else {
        key: spec["default"] for key, spec in schema.items() if "default" in spec})
    for key, value in changes.items():
        spec = schema[key]
        kind = spec.get("type", "string")
        valid = {
            "string": isinstance(value, str),
            "number": isinstance(value, (int, float)) and not isinstance(value, bool),
            "integer": isinstance(value, int) and not isinstance(value, bool),
            "boolean": isinstance(value, bool), "json": True,
        }.get(kind, False)
        if not valid:
            raise WorkbenchError(f"{key} 需要 {kind}")
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            if not math.isfinite(value):
                raise WorkbenchError(f"{key} 必须为有限数值")
            if "min" in spec and value < spec["min"]:
                raise WorkbenchError(f"{key} 不能小于 {spec['min']}")
            if "max" in spec and value > spec["max"]:
                raise WorkbenchError(f"{key} 不能大于 {spec['max']}")
        if "choices" in spec and value not in spec["choices"]:
            raise WorkbenchError(f"{key} 需要从 {spec['choices']} 中选择")
        if "pattern" in spec and not re.fullmatch(spec["pattern"], value):
            raise WorkbenchError(f"{key} 格式无效，需要匹配 {spec['pattern']}")
        if spec.get("format") == "points":
            if not isinstance(value, list) or any(
                not isinstance(point, list) or len(point) != 2 or any(
                    isinstance(coordinate, bool) or not isinstance(coordinate, (int, float))
                    or not math.isfinite(coordinate) for coordinate in point) for point in value):
                raise WorkbenchError(f"{key} 需要有限坐标列表，例如 [[300,200],[500,200]]")
        result[key] = copy.deepcopy(value)
    return result


def json_value(value: Any) -> Any:
    """Represent runtime values without losing raw bytes in the JSON API."""
    if isinstance(value, bytes):
        import base64
        return {"type": "bytes", "base64": base64.b64encode(value).decode("ascii"),
                "length": len(value)}
    if isinstance(value, dict):
        return {str(k): json_value(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [json_value(v) for v in value]
    if isinstance(value, float) and not math.isfinite(value):
        return None
    if hasattr(value, "tolist"):
        return json_value(value.tolist())
    return value


@dataclass(frozen=True)
class Port:
    """A named, directional port. Connections never perform implicit conversion."""

    id: str
    label: str
    direction: str
    data_type: str


@dataclass
class Definition:
    """One discoverable node type, including its runtime and available UI views."""

    id: str
    name: str
    category: str
    description: str
    ports: tuple[Port, ...]
    properties: dict[str, dict]
    factory: Callable
    views: dict[str, Callable] = field(default_factory=dict)
    actions: dict[str, dict] = field(default_factory=dict)
    version: str = "1.0"
    # IO nodes do not synchronously feed received TX back into their RX output.
    breaks_cycle: bool = False
    view_properties: dict[str, dict] = field(default_factory=dict)

    def defaults(self) -> dict:
        """Create independent configuration; mutable defaults never leak between nodes."""
        return {key: copy.deepcopy(spec.get("default")) for key, spec in self.properties.items()}

    def describe(self) -> dict:
        """Publish exactly the editable contract needed by an editor or AI client."""
        return {
            "id": self.id, "name": self.name, "category": self.category,
            "description": self.description, "version": self.version,
            "ports": [vars(p) for p in self.ports],
            "properties": self.properties, "views": list(self.views),
            "actions": self.actions, "breaks_cycle": self.breaks_cycle,
            "view_properties": self.view_properties,
        }

@dataclass
class WireDefinition:
    """Wire plugin: one-to-one connection appearance and path construction.

Data is transferred unchanged by the runtime. Processing belongs to node plugins.
"""

    id: str
    name: str
    properties: dict
    factory: Callable
    version: str = "1.0"

    def describe(self) -> dict:
        return {"id": self.id, "name": self.name, "version": self.version,
                "kind": "wire", "properties": self.properties}


@dataclass
class ContainerDefinition:
    """A layout plugin constructs an adaptive Qt container from child widgets."""

    id: str
    name: str
    properties: dict
    factory: Callable

    def describe(self) -> dict:
        return {"id": self.id, "name": self.name, "kind": "container",
                "properties": self.properties}


class Registry:
    """Load real plugin implementations and provide a single discovery surface."""

    def __init__(self):
        self.nodes: dict[str, Definition] = {}
        self.wires: dict[str, WireDefinition] = {}
        self.containers: dict[str, ContainerDefinition] = {}
        self.external: list[str] = []
        self.providers: dict[str, dict] = {}
        self.owners: dict[tuple[str, str], str] = {}
        self.module_roots: set[str] = set()

    def clone(self):
        """Keep independent catalogues while sharing immutable implementation snapshots."""
        registry = Registry()
        for kind in ("nodes", "wires", "containers"):
            setattr(registry, kind, getattr(self, kind).copy())
        registry.external = self.external.copy()
        registry.providers = self.providers.copy()
        registry.owners = self.owners.copy()
        registry.module_roots = self.module_roots.copy()
        return registry

    def register_provider(self, module, source, *, builtin=False):
        """Register atomically and retain the exact code/resources used by this provider."""
        source = Path(source).resolve(strict=True)
        files = plugin_files(source, getattr(module, "PROJECT_FILES", ()))
        before = {kind: getattr(self, kind).copy() for kind in ("nodes", "wires", "containers")}
        try:
            module.register(self)
        except Exception:
            for kind, definitions in before.items():
                setattr(self, kind, definitions)
            raise
        key = uuid.uuid4().hex
        definitions = {kind: sorted(getattr(self, kind).keys() - before[kind].keys()) for kind in before}
        self.providers[key] = {"entry": source.name, "files": files, "builtin": builtin,
                               "definitions": definitions}
        self.owners.update({(kind, ident): key for kind, ids in definitions.items() for ident in ids})
        return key

    def register(self, definition: Definition | WireDefinition | ContainerDefinition):
        """Reject ID collisions so a project cannot silently change implementation."""
        if isinstance(definition, Definition):
            target = self.nodes
        elif isinstance(definition, WireDefinition):
            target = self.wires
        elif isinstance(definition, ContainerDefinition):
            target = self.containers
        else:
            raise WorkbenchError("插件需要注册 Definition、WireDefinition 或 ContainerDefinition")
        if definition.id in target:
            raise WorkbenchError(f"插件 ID 已存在：{definition.id}")
        target[definition.id] = definition

    def load_external(self, path: str):
        """Import a project-local provider; relative helper imports stay in its namespace."""
        source = Path(path).resolve(strict=True)
        if source.suffix != ".py":
            raise WorkbenchError("外部插件入口必须是 .py 文件")
        namespace = "comtoolx._external_" + uuid.uuid4().hex
        package = types.ModuleType(namespace)
        package.__path__ = [str(source.parent)]
        sys.modules[namespace] = package
        try:
            spec = importlib.util.spec_from_file_location(namespace + "." + source.stem, source)
            if spec is None or spec.loader is None:
                raise WorkbenchError("无法加载插件入口")
            module = importlib.util.module_from_spec(spec)
            sys.modules[spec.name] = module
            spec.loader.exec_module(module)
            self.register_provider(module, source)
        except Exception:
            for name in list(sys.modules):
                if name == namespace or name.startswith(namespace + "."):
                    del sys.modules[name]
            raise
        self.module_roots.add(namespace)
        self.external.append(str(source))

    def describe(self) -> dict:
        return {"nodes": [d.describe() for d in self.nodes.values()],
                "wires": [d.describe() for d in self.wires.values()],
                "containers": [d.describe() for d in self.containers.values()],
                "external": self.external, "ui_properties": UI_PROPERTIES}


def plugin_files(source: Path, resources=()) -> dict[str, bytes]:
    """Capture an entry, local relative-import helpers and declared resources, without escaping its folder."""
    root = source.parent.resolve()
    pending = [source]
    if not isinstance(resources, (list, tuple)) or any(not isinstance(item, str) for item in resources):
        raise WorkbenchError("插件 PROJECT_FILES 需要相对文件或目录名列表")
    for relative in resources:
        path = (root / relative).resolve(strict=True)
        if not path.is_relative_to(root):
            raise WorkbenchError("插件资源不能位于插件目录之外")
        pending.extend(path.rglob("*") if path.is_dir() else [path])
    files = {}
    while pending:
        path = pending.pop().resolve(strict=True)
        if not path.is_relative_to(root):
            raise WorkbenchError("插件文件不能位于插件目录之外")
        if not path.is_file() or "__pycache__" in path.parts or path.suffix == ".pyc":
            continue
        relative = path.relative_to(root).as_posix()
        if relative in files:
            continue
        files[relative] = path.read_bytes()
        if path.suffix != ".py":
            continue
        for statement in ast.walk(ast.parse(files[relative], filename=str(path))):
            if not isinstance(statement, ast.ImportFrom) or not statement.level:
                continue
            parent = path.parent
            for _ in range(statement.level - 1):
                parent = parent.parent
            if not parent.is_relative_to(root):
                continue  # SDK imports remain supplied by the installed application.
            names = [statement.module] if statement.module else [alias.name for alias in statement.names]
            for name in names:
                base = parent.joinpath(*name.split("."))
                candidates = [base.with_suffix(".py"), base / "__init__.py"]
                if statement.module:
                    candidates.extend(base / (alias.name + ".py") for alias in statement.names)
                pending.extend(candidate for candidate in candidates if candidate.is_file())
    return files


class NodeRuntime:
    """Base runtime owns state synchronization and checked output publication.

Receiving, actions and IO are implemented by concrete plugins. Qt widgets must
only read snapshots or request actions; worker threads never manipulate widgets.
"""

    def __init__(self, node: dict, engine):
        self.node = node
        self.id = node["id"]
        self.config = copy.deepcopy(node["config"])
        self.engine = engine
        self.lock = threading.RLock()
        self.stop_event = threading.Event()
        self.state = {"active": False, "received": 0, "sent": 0, "error": ""}

    def start(self):
        with self.lock:
            self.stop_event.clear()
            self.state["active"] = True

    def stop(self):
        with self.lock:
            self.stop_event.set()
            self.state["active"] = False

    def receive(self, port: str, data: Any):
        raise WorkbenchError(f"{self.node['plugin']} 没有实现输入 {port}")

    def action(self, name: str, args: dict):
        raise WorkbenchError(f"{self.node['plugin']} 不支持操作 {name}")

    def emit(self, port: str, data: Any):
        """Check output declarations and enqueue unchanged data on the graph worker."""
        if not self.engine.running:
            raise WorkbenchError("请先运行功能图")
        definition = self.engine.registry.nodes[self.node["plugin"]]
        if not any(p.id == port and p.direction == "out" for p in definition.ports):
            raise WorkbenchError(f"{self.id} 没有输出端口 {port}")
        with self.lock:
            self.state["sent"] += 1
        return self.engine.publish(self.id, port, data)

    def snapshot(self) -> dict:
        with self.lock:
            return copy.deepcopy(self.state)
