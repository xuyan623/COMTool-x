"""Exercise the real native desktop through its public interface.

Run against a dedicated build/qa-ui instance. The script refuses to replace a
dirty document and writes all projects/captures into the chosen output folder.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import time
from urllib.error import HTTPError
from urllib.request import Request, urlopen


class Client:
    """Keep credentials private while waiting for actual background completion."""

    def __init__(self, directory):
        record = json.loads((Path(directory) / ".sessions/api.json").read_text("utf-8"))
        self.url, self.token = record["url"], record["token"]

    def call(self, operation, arguments=None, *, wait=True):
        payload = json.dumps({"op": operation, "args": arguments or {}}, ensure_ascii=False).encode()
        request = Request(self.url + "/v3/command", payload,
                          {"Authorization": "Bearer " + self.token, "Content-Type": "application/json"})
        try:
            with urlopen(request, timeout=10) as response:
                value = json.load(response)
        except HTTPError as error:
            value = json.load(error)
        if not value.get("ok"):
            raise RuntimeError(operation + ": " + value.get("error", "unknown failure"))
        result = value.get("result")
        if wait and isinstance(result, dict) and (result.get("job") or result.get("jobs")):
            deadline = time.monotonic() + 30
            for ident in [result["job"]] if result.get("job") else result["jobs"]:
                while time.monotonic() < deadline:
                    job = self.call("job.get", {"id": ident}, wait=False)
                    if job["state"] == "complete":
                        if result.get("job"):
                            return job.get("result")
                        break
                    if job["state"] in ("failed", "missing"):
                        raise RuntimeError(operation + ": " + job.get("error", "job failed"))
                    time.sleep(.01)
                else:
                    raise TimeoutError(operation)
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    client = Client(args.data_dir)
    state = client.call("app.state")
    if state["dirty"]:
        raise RuntimeError("The test instance has unsaved changes; refusing to replace them")
    client.call("project.new", {"name": "布局与调试验收"})
    client.call("log.clear")
    checks = []

    def check(condition, label):
        if not condition:
            raise AssertionError(label)
        checks.append(label)
        print("PASS", label, flush=True)

    # Detaching the main shell's only tab leaves a compact independent window and
    # an empty, usable main shell, rather than creating another full application.
    main_id = client.call("app.state")["workspaces"]["windows"][0]["id"]
    detached = client.call("workspace.detach", {"page": "graph", "x": 250, "y": 160})
    child_id = detached["window"]
    windows = client.call("app.state")["workspaces"]["windows"]
    child = next(w for w in windows if w["id"] == child_id)
    check(len(windows) == 2 and child["child"], "only tab detaches into a compact independent window")
    check(not child["native_owned"] and not child["native_topmost"], "detached pages have no native owner or implicit always-on-top flag")
    # Focus/raise alternation must allow either normal window to come forward.
    for ident in (child_id, main_id, child_id, main_id):
        client.call("window.configure", {"window": ident, "focus": True})
        time.sleep(.08)
        states = client.call("app.state")["workspaces"]["windows"]
        check(next(w for w in states if w["id"] == ident)["focused"], "native focus follows the requested main/detached window")
    check(any(c["id"] == "execution.run" and c["visible"] for c in client.call("controls.list")),
          "engine controls remain in the empty main shell after its only page detaches")
    client.call("window.pin", {"window": child_id, "enabled": True})
    time.sleep(.08)
    child = next(w for w in client.call("app.state")["workspaces"]["windows"] if w["id"] == child_id)
    check(child["pinned"] and child["native_topmost"], "child topmost flag reaches the native window")
    client.call("window.pin", {"window": child_id, "enabled": False})
    client.call("workspace.move", {"page": "graph", "window": main_id})
    check(len(client.call("app.state")["workspaces"]["windows"]) == 1, "reattaching removes the empty child")
    library = client.call("workspace.open", {"page": "library"})
    windows = client.call("app.state")["workspaces"]["windows"]
    browser = next(w for w in windows if w["id"] == library["window"])
    check(browser["child"] and browser["tabs"] == ["library"], "library opens directly in a compact child")
    check(client.call("workspace.open", {"page": "library"})["window"] == browser["id"], "reopening the library focuses the same child")
    client.call("workspace.close", {"page": "library"})

    commands = []
    for i, ident in enumerate(("h1", "h2", "v1", "v2")):
        commands.append({"op": "graph.node.add", "args": {"id": ident, "plugin": "display.value", "title": ident,
                         "position": [80 + i * 220, 340], "config": {"label": ident}}})
    commands.extend([
        {"op": "graph.node.add", "args": {"id": "constant", "plugin": "source.constant", "title": "常量", "position": [80, 80], "config": {"values": {"value": 1.25}}}},
        {"op": "graph.node.add", "args": {"id": "sine", "plugin": "function.sin", "title": "正弦", "position": [370, 80]}},
        {"op": "graph.node.add", "args": {"id": "result", "plugin": "display.value", "title": "计算结果", "position": [660, 80]}},
        {"op": "graph.wire.add", "args": {"id": "a", "source": ["constant", "value"], "target": ["sine", "x"]}},
        {"op": "graph.wire.add", "args": {"id": "b", "source": ["sine", "out"], "target": ["result", "value"]}},
        {"op": "ui.container.add", "args": {"id": "horizontal", "plugin": "layout.row", "properties": {"title": "横向分区：左右排列", "min_height": 130}}},
        {"op": "ui.container.add", "args": {"id": "vertical", "plugin": "layout.column", "properties": {"title": "纵向分区：上下排列", "min_height": 230}}},
    ])
    for ident, parent in (("h1", "horizontal"), ("h2", "horizontal"), ("v1", "vertical"), ("v2", "vertical")):
        commands.append({"op": "ui.view.add", "args": {"id": ident + "_view", "parent": parent, "node": ident, "view": "control", "properties": {"title": ident}}})
    client.call("batch", {"commands": commands})
    client.call("workspace.open", {"page": "layout"})
    time.sleep(.3)
    controls = client.call("controls.list")
    bounds = {item["id"]: item["bounds"] for item in controls if item.get("visible") and item["id"].startswith("ui.")}
    h1, h2, v1, v2 = (bounds["ui." + ident + "_view"] for ident in ("h1", "h2", "v1", "v2"))
    check(abs(h1[1] - h2[1]) < 2 and h2[0] >= h1[0] + h1[2] - 1, "horizontal children actually sit side by side")
    check(abs(v1[0] - v2[0]) < 2 and v2[1] >= v1[1] + v1[3] - 1, "vertical children actually stack")
    client.call("capture", {"target": "layout", "path": str(output / "layout.png")})
    client.call("workspace.open", {"page": "graph"})
    client.call("graph.select", {"id": "a", "kind": "wire"})
    handles = client.call("graph.wire.handles", {"id": "a"})
    check(len(handles["handles"]) == 1 and handles["handles"][0]["insertion"], "selecting an automatic wire exposes its insertion handle")
    client.call("graph.wire.patch", {"id": "a", "changes": {"points": [[295, 200], [325, 235]], "routing": "polyline"}})
    handles = client.call("graph.wire.handles", {"id": "a"})
    check(handles["routing"] == "polyline" and sum(not h["insertion"] for h in handles["handles"]) == 2 and sum(h["insertion"] for h in handles["handles"]) == 3,
          "manual points and every segment midpoint remain editable")
    client.call("history.undo")
    check(client.call("graph.wire.handles", {"id": "a"})["points"] == [], "wire shape changes undo as one operation")

    client.call("runtime.debug", {"enabled": True})
    client.call("runtime.step", {"mode": "node", "source": "constant"})
    runtime = client.call("runtime.state")
    check(runtime["debug"] and runtime["current_node"] == "constant" and runtime["cursor"]["next_node"] == "sine", "debug node step publishes the real next-node cursor")
    client.call("runtime.step", {"mode": "node"})
    client.call("runtime.step", {"mode": "node"})
    runtime = client.call("runtime.state")
    check(not runtime["cursor"] and abs(runtime["nodes"]["sine"]["outputs"]["out"] - math.sin(1.25)) < 1e-12, "node steps complete exactly one coherent batch")
    logs = client.call("log.query", {"batch": 1, "limit": 100})["records"]
    check(sum(r.get("kind") == "node" for r in logs) == 3 and all(r.get("batch") == 1 for r in logs), "debug journal retains inputs, outputs and batch filtering")
    client.call("log.configure", {"detail": True})
    check(client.call("runtime.state")["log_detail"], "normal-run detailed journal can be enabled")
    client.call("workspace.open", {"page": "graph"})
    client.call("graph.select", {"id": "sine"})
    client.call("graph.fit")
    time.sleep(.2)
    client.call("capture", {"target": "graph", "path": str(output / "graph.png")})
    controls = client.call("controls.list")
    names = {c["id"] for c in controls}
    check(all("execution." + key in names for key in ("run", "pause", "debug", "batch", "node", "reset", "end", "source", "status")), "the full shared engine toolbar is exposed through actual controls")

    # Switching pages or detaching the only page must leave one global command
    # surface in the main shell, independent of each page's local tools.
    for page in ("layout", "logs", "runtime"):
        client.call("workspace.open", {"page": page})
        controls = client.call("controls.list")
        run = [c for c in controls if c["id"] == "execution.run" and c["visible"]]
        check(len(run) == 1, "shared engine controls remain visible on " + page)
    # Grid placement is polished on the next Qt Quick frame, after the command
    # has finished. Wait for real geometry rather than reading its initial zeros.
    deadline = time.monotonic() + 2
    horizontal = False
    while time.monotonic() < deadline:
        tabs = {c["id"]: c["bounds"] for c in client.call("controls.list") if c["id"].startswith("workspace.tab.")}
        graph, layout, runtime = (tabs["workspace.tab." + p] for p in ("graph", "layout", "runtime"))
        horizontal = max(graph[1], layout[1], runtime[1]) - min(graph[1], layout[1], runtime[1]) < 1 and len({round(graph[0]), round(layout[0]), round(runtime[0])}) == 3
        if horizontal:
            break
        time.sleep(.025)
    check(horizontal, "main workspace tabs form a horizontal strip above the workspace")

    client.call("workspace.open", {"page": "runtime"})
    detached = client.call("workspace.detach", {"page": "runtime", "x": 250, "y": 180})
    client.call("window.pin", {"window": detached["window"], "enabled": True})
    time.sleep(.2)
    client.call("capture", {"target": "runtime", "path": str(output / "runtime-child.png")})
    client.call("workspace.move", {"page": "runtime", "window": main_id})
    check(client.call("runtime.state")["active"], "moving and closing child windows preserves the shared session")
    client.call("runtime.stop")
    client.call("project.save", {"path": str(output / "InterfaceAcceptance.comtoolx")})
    client.call("workspace.open", {"page": "graph"})
    client.call("graph.select", {"id": "sine"})
    (output / "result.json").write_text(json.dumps({"checks": checks, "count": len(checks)}, ensure_ascii=False, indent=2), "utf-8")
    print(json.dumps({"passed": len(checks), "output": str(output)}, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()
