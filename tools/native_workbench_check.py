"""Verify the real workbench model and leave an independent UI inspection project.

All mutations target the explicitly supplied test instance. Hardware transports
are never opened. Screenshots and the saved project stay in the output folder.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import time

from native_ui_check import Client


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    client = Client(args.data_dir)
    if client.call("app.state")["dirty"]:
        raise RuntimeError("Refusing to replace the test instance's unsaved document")
    checks = []

    def check(condition, label):
        if not condition:
            raise AssertionError(label)
        checks.append(label)
        print("PASS", label, flush=True)

    client.call("project.new", {"name": "胶囊节点与设计工作流验收"})
    commands = []
    for ident, plugin, xy, config in [
        ("constant", "source.constant", [30, 40], {"values": {"value": 1.25}}),
        ("exponential", "function.exp", [360, 40], {}),
        ("result", "display.value", [790, 40], {"label": "指数运算结果"}),
        ("text", "control.text", [30, 260], {"value": "CoRig"}),
        ("button", "control.button", [360, 260], {}),
        ("sine", "function.sin", [650, 260], {}),
    ]:
        commands.append({"op": "graph.node.add", "args": {"id": ident, "plugin": plugin, "position": xy, "config": config}})
    client.call("batch", {"commands": commands})
    client.call("editor.add", {"page": "graph", "plugin": "control.number", "id": "pending_number"})
    project = client.call("project.get")
    check(next(n for n in project["nodes"] if n["id"] == "pending_number")["placed"] is False,
          "library additions remain pending")
    check("pending_number" not in client.call("runtime.state").get("nodes", {}),
          "pending graph nodes do not enter computation")
    client.call("editor.place", {"page": "graph", "id": "pending_number", "position": [30, 460]})
    client.call("editor.withdraw", {"page": "graph", "id": "pending_number"})
    check("pending_number" not in client.call("runtime.state").get("nodes", {}),
          "withdrawing preserves design and removes the runtime node")
    client.call("graph.wire.add", {"id": "compute", "source": ["constant", "value"], "target": ["exponential", "x"]})
    client.call("graph.wire.add", {"id": "observe", "source": ["exponential", "out"], "target": ["result", "value"]})

    plan = client.call("sync.plan", {"direction": "graph_to_ui"})
    keys = [c["key"] for c in plan["changes"] if c["node"] != "pending_number"]
    client.call("sync.apply", {"direction": plan["direction"], "keys": keys, "revision": plan["revision"]})
    project = client.call("project.get")
    bound = {i["node"]: i["id"] for i in project["ui"]["items"] if "node" in i}
    pending = next(i for i in project["ui"]["items"] if i["id"] == "pending")["children"]
    check(set(pending) == set(bound.values()) and "pending_number" not in bound,
          "selected synchronization adds only checked objects to pending")
    client.call("editor.add", {"page": "layout", "plugin": "layout.row", "id": "row"})
    client.call("editor.place", {"page": "layout", "id": "row", "parent": "root", "position": [70, 70]})
    client.call("ui.item.patch", {"id": "row", "properties": {"width": 840, "height": 210, "title": "输入与操作"}})
    for index, node in enumerate(("text", "button")):
        client.call("editor.place", {"page": "layout", "id": bound[node], "parent": "row", "index": index})
    client.call("editor.place", {"page": "layout", "id": bound["result"], "parent": "root", "position": [70, 340]})
    client.call("ui.item.patch", {"id": bound["result"], "properties": {"width": 480, "height": 150, "font_size": 28, "title": "结果显示"}})
    before = client.call("project.get")
    plan = client.call("sync.plan", {"direction": "graph_to_ui"})
    check(not any(c["node"] in ("text", "button", "result") for c in plan["changes"]),
          "repeat synchronization preserves existing objects and layout")
    client.call("editor.copy", {"page": "layout", "ids": [bound["result"]]})
    client.call("editor.paste", {"page": "layout"})
    after = client.call("project.get")
    original_ids = {n["id"] for n in before["nodes"]}
    copied = [n for n in after["nodes"] if n["id"] not in original_ids]
    check(len(copied) == 1 and copied[0]["placed"] is False and after["wires"] == before["wires"],
          "independent UI copies create a new pending node without external wires")
    copied_view = next(i for i in after["ui"]["items"] if i.get("node") == copied[0]["id"])
    check(copied_view["properties"]["font_size"] == 28, "independent copies inherit presentation")
    client.call("editor.remove", {"page": "layout", "ids": [copied_view["id"]], "confirmed": True})
    plan = client.call("sync.plan", {"direction": "ui_to_graph"})
    deletion = next(c for c in plan["changes"] if c["node"] == copied[0]["id"])
    check(deletion["action"] == "remove", "target-only objects appear as explicit deletion choices")
    client.call("sync.apply", {"direction": plan["direction"], "keys": [deletion["key"]], "revision": plan["revision"]})
    config_before = next(n for n in client.call("project.get")["nodes"] if n["id"] == "text")["config"]
    try:
        client.call("ui.item.patch", {"id": bound["text"], "properties": {"value": "forbidden override"}})
        raise AssertionError("UI parameters incorrectly overrode functional parameters")
    except RuntimeError:
        pass
    check(next(n for n in client.call("project.get")["nodes"] if n["id"] == "text")["config"] == config_before,
          "presentation properties cannot overwrite functional values")
    client.call("runtime.start", {"debug": False})
    time.sleep(.15)
    client.call("runtime.pause")
    state = client.call("node.state", {"node": "result"})
    value = state.get("inputs", {}).get("value")
    check(isinstance(value, (int, float)) and abs(value - math.exp(1.25)) < 1e-10,
          "native computation still propagates real exponential results")
    client.call("runtime.stop")

    # The remaining nodes are intentionally unconnected for manual bubble tests.
    client.call("workspace.open", {"page": "graph"})
    client.call("graph.fit")
    client.call("graph.select", {"id": "exponential"})
    client.call("capture", {"target": "graph", "path": str(output / "graph.png")})
    client.call("workspace.open", {"page": "layout"})
    time.sleep(.2)
    client.call("capture", {"target": "layout", "path": str(output / "layout.png")})
    client.call("workspace.open", {"page": "runtime"})
    time.sleep(.2)
    client.call("capture", {"target": "runtime", "path": str(output / "runtime.png")})
    project = client.call("project.get")
    saved = output / "Workbench.corig"
    client.call("project.save", {"path": str(saved)})
    client.call("project.open", {"path": str(saved)})
    check(client.call("project.get") == project, "placement, layers and bindings survive save/reopen")
    client.call("workspace.open", {"page": "graph"})
    controls = client.call("controls.list")
    toolbar = next(c for c in controls if c["id"] == "execution.toolbar")
    graph = next(c for c in controls if c["id"] == "page.graph")
    check(toolbar["bounds"][1] >= graph["bounds"][1] + graph["bounds"][3] - 2,
          "shared engine controls are below the workspace")
    (output / "report.json").write_text(json.dumps({"checks": checks, "count": len(checks), "project": str(saved)}, ensure_ascii=False, indent=2), "utf-8")


if __name__ == "__main__":
    main()
