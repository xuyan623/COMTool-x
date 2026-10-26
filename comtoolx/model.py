"""Serializable project model and validation; no IO occurs while opening a graph."""

from __future__ import annotations

import copy
import json
import math
import uuid
import zipfile
from pathlib import Path

from .sdk import Registry, WorkbenchError, UI_PROPERTIES, checked_properties

FORMAT = "comtool-x"
SCHEMA = 1


def new_id(prefix: str) -> str:
    """Generate stable object identities independent of labels or canvas positions."""
    return f"{prefix}_{uuid.uuid4().hex[:10]}"


def new_project(name: str = "未命名工程") -> dict:
    """A new project is editable, disconnected and contains only a layout container."""
    return {"format": FORMAT, "schema_version": SCHEMA, "name": name,
            "theme": "dark", "nodes": [], "wires": [],
            "ui": {"root": "root", "items": [
                {"id": "root", "plugin": "layout.row", "parent": None,
                 "title": "主界面", "stretch": 1, "properties": {"spacing": 12, "margin": 12}}
            ]}, "metadata": {}}


def node_by_id(project: dict, node_id: str) -> dict:
    """Resolve IDs without confusing repeated plugin instances with plugin types."""
    for node in project["nodes"]:
        if node["id"] == node_id:
            return node
    raise WorkbenchError(f"节点不存在：{node_id}")


def ui_by_id(project: dict, item_id: str) -> dict:
    for item in project["ui"]["items"]:
        if item["id"] == item_id:
            return item
    raise WorkbenchError(f"UI 对象不存在：{item_id}")


def validate(project: dict, registry: Registry) -> list[dict]:
    """Report missing plugins, invalid bindings, port types and synchronous cycles."""
    problems = []
    def report(code, message, object_id=""):
        problems.append({"code": code, "message": message, "object_id": object_id})
    ids = set()
    nodes = {}
    for node in project.get("nodes", []):
        ident = node.get("id")
        if not ident or ident in ids:
            report("node_id", "节点 ID 缺失或重复", ident)
        ids.add(ident)
        nodes[ident] = node
        definition = registry.nodes.get(node.get("plugin"))
        if definition is None:
            report("missing_plugin", f"缺少插件：{node.get('plugin')}", ident)
        else:
            try:
                checked_properties(definition.properties, node.get("config", {}))
            except ValueError as exc:
                report("config", str(exc), ident)
        position = node.get("position", [0, 0])
        rotation = node.get("rotation", 0)
        if isinstance(rotation, bool) or not isinstance(rotation, int) or rotation not in (0, 90, 180, 270):
            report("rotation", "节点方向需要 0、90、180 或 270 度", ident)
        if (not isinstance(position, list) or len(position) != 2 or
                any(not isinstance(v, (int, float)) or not math.isfinite(v) for v in position)):
            report("position", "节点坐标需要两个有限数值", ident)
    endpoints = set()
    wire_ids = set()
    adjacency = {key: [] for key in nodes}
    for wire in project.get("wires", []):
        wid = wire.get("id", "")
        if not wid or wid in wire_ids:
            report("wire_id", "导线 ID 缺失或重复", wid)
        wire_ids.add(wid)
        if wire.get("plugin") not in registry.wires:
            report("missing_wire", f"缺少导线插件：{wire.get('plugin')}", wid)
        else:
            try:
                checked_properties(registry.wires[wire["plugin"]].properties,
                                   wire.get("properties", {}))
            except ValueError as exc:
                report("wire_properties", str(exc), wid)
        resolved = []
        for side, direction in (("source", "out"), ("target", "in")):
            endpoint = wire.get(side, {})
            nid, pid = endpoint.get("node"), endpoint.get("port")
            marker = (nid, pid, direction)
            if marker in endpoints:
                report("port_occupied", "首版每个端口只允许一条导线", wid)
            endpoints.add(marker)
            node = nodes.get(nid)
            definition = registry.nodes.get(node.get("plugin")) if node else None
            port = next((p for p in definition.ports
                         if p.id == pid and p.direction == direction), None) if definition else None
            if port is None:
                report("port", f"{side} 端口不存在或方向不匹配：{nid}.{pid}", wid)
            resolved.append(port)
        if all(resolved):
            source, target = resolved
            if target.data_type != "any" and source.data_type != target.data_type:
                report("data_type", f"数据类型不匹配：{source.data_type} → {target.data_type}", wid)
            source_id = wire["source"]["node"]
            definition = registry.nodes[nodes[source_id]["plugin"]]
            if not definition.breaks_cycle:
                adjacency[source_id].append(wire["target"]["node"])
    visiting, visited = set(), set()
    def walk(nid):
        if nid in visiting:
            report("cycle", "同步处理回路需要通过异步 IO 节点断开", nid)
            return
        if nid in visited:
            return
        visiting.add(nid)
        for target in adjacency.get(nid, []):
            walk(target)
        visiting.remove(nid)
        visited.add(nid)
    for nid in nodes:
        walk(nid)
    ui_items = project.get("ui", {}).get("items", [])
    ui_ids = {item.get("id") for item in ui_items}
    if len(ui_ids) != len(ui_items) or None in ui_ids:
        report("ui_id", "UI 对象 ID 缺失或重复")
    root = project.get("ui", {}).get("root")
    if root not in ui_ids:
        report("ui_root", "UI 根容器不存在")
    parents = {item.get("id"): item.get("parent") for item in ui_items}
    for item in ui_items:
        ident = item.get("id", "")
        container = item.get("plugin")
        if container:
            definition = registry.containers.get(container)
            if container not in registry.containers:
                report("ui_plugin", f"缺少布局插件：{container}", ident)
        else:
            node = nodes.get(item.get("node"))
            definition = registry.nodes.get(node.get("plugin")) if node else None
            if not definition or item.get("view") not in definition.views:
                report("ui_binding", "UI 绑定的节点或视图已失效，请更新 UI", ident)
        if definition:
            schema = (definition.properties if container else
                      definition.view_properties.get(item.get("view"), {}))
            try:
                checked_properties({**UI_PROPERTIES, **schema}, item.get("properties", {}))
                stretch = item.get("stretch", 1)
                if not isinstance(stretch, int) or isinstance(stretch, bool) or not 0 <= stretch <= 10000:
                    raise WorkbenchError("拉伸权重需要 0～10000 的整数")
            except ValueError as exc:
                report("ui_properties", str(exc), ident)
        parent_item = next((entry for entry in ui_items if entry.get("id") == item.get("parent")), None)
        if ident == root and (not container or item.get("parent") is not None):
            report("ui_root", "根对象必须是无父级的布局容器", ident)
        if ident != root and parent_item and not parent_item.get("plugin"):
            report("ui_parent", "父对象必须是布局容器", ident)
        if ident != root and item.get("parent") not in ui_ids:
            report("ui_parent", "UI 父容器不存在", ident)
        ancestor, chain = item.get("parent"), {ident}
        while ancestor is not None:
            if ancestor in chain:
                report("ui_cycle", "UI 容器不能循环包含", ident)
                break
            chain.add(ancestor)
            ancestor = parents.get(ancestor)
    return problems


def load_project(path: str) -> dict:
    """Inspect design data from a .comtoolx bundle without executing its plugins."""
    if Path(path).suffix.lower() != ".comtoolx" or not zipfile.is_zipfile(path):
        raise WorkbenchError("请选择 .comtoolx 工程包；不支持 JSON 工程")
    with zipfile.ZipFile(path) as archive:
        if archive.getinfo("project.json").file_size > 16 * 1024 * 1024:
            raise WorkbenchError("工程设计数据过大")
        project = json.loads(archive.read("project.json"))
    if project.get("format") != FORMAT or project.get("schema_version") != SCHEMA:
        raise WorkbenchError("工程设计版本不受支持")
    for field in ("nodes", "wires"):
        if not isinstance(project.get(field), list):
            raise WorkbenchError(f"工程缺少 {field} 列表")
    if not isinstance(project.get("ui", {}).get("items"), list):
        raise WorkbenchError("工程缺少 UI 布局")
    return project


def update_ui(project: dict, registry: Registry) -> dict:
    """Explicit graph-to-UI update preserves existing layout, IDs and presentation.

New views enter the pending container. Invalid bindings remain visible as issues
until the designer explicitly removes or rebinds them.
"""
    items = project["ui"]["items"]
    available = {(item.get("node"), item.get("view")) for item in items}
    available.update((item["node"], item["view"]) for item in project["ui"].get("hidden", []))
    pending = next((item for item in items if item["id"] == "pending"), None)
    added = []
    for node in project["nodes"]:
        definition = registry.nodes.get(node["plugin"])
        if not definition:
            continue
        for view in definition.views:
            if (node["id"], view) in available:
                continue
            if pending is None:
                pending = {"id": "pending", "plugin": "layout.column",
                           "parent": project["ui"]["root"], "title": "待放置",
                           "stretch": 1, "properties": {"spacing": 10, "margin": 8}}
                items.append(pending)
            item = {"id": new_id("view"), "node": node["id"], "view": view,
                    "parent": "pending", "title": node["name"],
                    "stretch": 1, "properties": {}}
            items.append(item)
            added.append(item["id"])
    issues = [p for p in validate(project, registry) if p["code"].startswith("ui_")]
    return {"added": added, "issues": issues}
