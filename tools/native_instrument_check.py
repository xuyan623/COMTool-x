"""Verify native instruments and packaged Python/QML extensions with real data.

The dedicated instance must have a saved document. A temporary local SSH server
exercises the production SSH client without contacting hardware or remote hosts.
"""
from __future__ import annotations

import argparse
import base64
import json
import math
from pathlib import Path
import socket
import threading
import time

from native_ui_check import Client


class LocalSsh:
    """Provide an actual authenticated SSH shell on a loopback-only socket."""

    def __init__(self):
        import paramiko

        self.paramiko = paramiko
        self.key = paramiko.RSAKey.generate(2048)
        self.socket = socket.socket()
        self.socket.bind(("127.0.0.1", 0))
        self.socket.listen(4)
        self.socket.settimeout(.2)
        self.port = self.socket.getsockname()[1]
        self.closed = threading.Event()
        self.sizes = []
        self.received = bytearray()
        self.thread = threading.Thread(target=self.serve, daemon=True)
        self.thread.start()

    def serve(self):
        paramiko, owner = self.paramiko, self

        class Server(paramiko.ServerInterface):
            def check_auth_password(self, username, password):
                return paramiko.AUTH_SUCCESSFUL if (username, password) == ("qa", "local-test") else paramiko.AUTH_FAILED

            def get_allowed_auths(self, username):
                return "password"

            def check_channel_request(self, kind, channel_id):
                return paramiko.OPEN_SUCCEEDED if kind == "session" else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

            def check_channel_pty_request(self, channel, term, width, height, pw, ph, modes):
                owner.sizes.append([width, height])
                return True

            def check_channel_window_change_request(self, channel, width, height, pw, ph):
                owner.sizes.append([width, height])
                return True

            def check_channel_shell_request(self, channel):
                return True

        while not self.closed.is_set():
            transport = None
            try:
                connection, _ = self.socket.accept()
                transport = paramiko.Transport(connection)
                transport.add_server_key(self.key)
                transport.start_server(server=Server())
                channel = transport.accept(5)
                if not channel:
                    continue
                channel.settimeout(.2)
                channel.sendall("\x1b[2J\x1b[H\x1b[32mSSH 中文终端 ready\x1b[0m\r\n".encode())
                while transport.is_active() and not self.closed.is_set():
                    try:
                        data = channel.recv(65536)
                        if not data:
                            break
                        self.received.extend(data)
                        channel.sendall(b"echo: " + data)
                    except socket.timeout:
                        pass
            except (socket.timeout, EOFError, OSError, paramiko.SSHException):
                pass
            finally:
                if transport:
                    transport.close()

    def close(self):
        self.closed.set()
        self.thread.join(2)
        self.socket.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--python", required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    c = Client(args.data_dir)
    if c.call("app.state")["dirty"]:
        raise RuntimeError("Refusing to replace unsaved design")
    checks = []

    def check(value, message):
        if not value:
            raise AssertionError(message)
        checks.append(message)
        print("PASS", message, flush=True)

    def until(predicate):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            result = predicate()
            if result:
                return result
            time.sleep(.025)
        raise TimeoutError("Waiting for data propagation")

    def action(node, name, **arguments):
        return c.call("node.action", {"node": node, "action": name, "args": arguments})

    def view(node, name, **arguments):
        return c.call("view.read", {"node": node, "view": name, "args": arguments})

    server = LocalSsh()
    try:
        c.call("preferences.set", {"python": str(args.python.resolve()), "read_only": False})
        c.call("project.new", {"name": "仪表与用户插件验收"})
        manifest = {
            "plugin_api": 3, "id": "user.qa.scale", "name": "本地 Python 倍率",
            "ports": [{"id": "x", "direction": "in", "data_type": "number"},
                      {"id": "out", "direction": "out", "data_type": "number"}],
            "properties": {}, "views": {"status": {"qml": "Status.qml"}},
            "code": "from .helper import scale\ndef process(inputs,state):\n    state['calls']=state.get('calls',0)+1\n    print('qa relative import')\n    return {'out':scale(inputs['x'])}\n",
            "resources": {"helper.py": "def scale(x): return x * 3\n",
                          "Status.qml": "import QtQuick\nimport QtQuick.Controls\nLabel { required property var bridge; objectName: 'qa.user.label'; text: 'Python: '+JSON.stringify(bridge.state.state||{}); color: '#66B4F4'; font.pixelSize: 18 }"}}
        c.call("plugin.import", {"manifest": manifest})
        operations = []

        def node(id, plugin, config=None, x=0, y=0):
            operations.append({"op": "graph.node.add", "args": {"id": id, "plugin": plugin, "config": config or {}, "position": [x, y]}})

        def wire(id, source, target):
            operations.append({"op": "graph.wire.add", "args": {"id": id, "source": source, "target": target}})

        node("serial", "io.serial", {"port": "loop://"}, 0, 0)
        node("decode", "protocol.decode", {"format": "firewater", "channels": ["sine", "cosine"], "channel_count": 3, "timestamp_index": 0}, 250, 0)
        node("plot", "plot.curves", {"inputs": ["user"], "max_points": 5000}, 500, 0)
        node("yaw", "control.number", {"value": 0, "minimum": -360, "maximum": 360}, 0, 180)
        node("attitude", "plot.attitude", {"angle_unit": "deg"}, 250, 180)
        node("number", "control.number", {"value": 2}, 0, 360)
        node("user", "user.qa.scale", {}, 250, 360)
        node("ssh", "io.ssh", {"host": "127.0.0.1", "port": server.port, "user": "qa", "password": "local-test"}, 0, 540)
        node("terminal", "terminal.ansi", {}, 250, 540)
        wire("csv-in", ["serial", "rx"], ["decode", "bytes"])
        wire("csv-plot", ["decode", "samples"], ["plot", "samples"])
        wire("yaw-attitude", ["yaw", "value"], ["attitude", "yaw"])
        wire("user-in", ["number", "value"], ["user", "x"])
        wire("user-out", ["user", "out"], ["plot", "user"])
        wire("ssh-rx", ["ssh", "rx"], ["terminal", "rx"])
        wire("ssh-tx", ["terminal", "tx"], ["ssh", "tx"])
        wire("ssh-size", ["terminal", "resize"], ["ssh", "resize"])
        c.call("batch", {"commands": operations})
        c.call("ui.update")
        c.call("ui.container.add", {"id": "top", "parent": "root", "plugin": "layout.row", "properties": {"min_height": 330}})
        c.call("ui.container.add", {"id": "bottom", "parent": "root", "plugin": "layout.row", "properties": {"min_height": 250}})
        # Reuse the views created by ui.update rather than duplicating them.
        c.call("ui.container.add", {"id": "controls", "parent": "root", "plugin": "layout.row", "properties": {"min_height": 130, "max_height": 150}})
        for item in c.call("project.get")["ui"]["items"]:
            if "node" in item:
                parent = "top" if item["node"] in ("plot", "attitude") else "bottom" if item["node"] in ("terminal", "user") else "controls"
                c.call("ui.item.move", {"id": item["id"], "parent": parent})
        c.call("ui.item.patch", {"id": "pending", "properties": {"title": "", "padding": 0, "min_height": 1, "max_height": 1}})
        c.call("workspace.open", {"page": "runtime"})
        c.call("runtime.start")
        action("serial", "connect")
        frames = "".join(f"{i/256},{math.sin(2*math.pi*16*i/256)},{math.cos(2*math.pi*16*i/256)}\n" for i in range(1024))
        action("serial", "send", text=frames)
        data = until(lambda: (result if (result := action("plot", "read_data"))["channels"].get("sine", {}).get("total", 0) == 1024 else None))
        check(data["channels"]["cosine"]["total"] == 1024, "loopback CSV delivers 1024 exact raw samples per channel")
        time_view = view("plot", "plot", width=120, follow=True)
        check(len(time_view["channels"][0]["points"]) <= 242, "display decimation preserves complete retained data")
        fft = view("plot", "plot", fft=True)
        sine = next(channel for channel in fft["channels"] if channel["name"] == "sine")
        peak = max(sine["points"][1:], key=lambda p: p[1])
        check(abs(peak[0]-16) < .3, "FFT reports the actual 16 Hz input peak")
        action("plot", "pause_display", value=True)
        frozen = view("plot", "plot")
        action("serial", "send", text="4,0.25,0.5\n")
        until(lambda: action("plot", "read_data")["channels"]["sine"]["total"] == 1025)
        check(view("plot", "plot")["version"] == frozen["version"], "pause drawing freezes display while acquisition continues")
        action("plot", "pause_display", value=False)
        action("plot", "export_csv", path=str(output / "samples.csv"))
        check(len((output / "samples.csv").read_text("utf-8").splitlines()) >= 2051, "CSV export contains raw retained points")
        action("yaw", "set_value", value=90)
        attitude = until(lambda: (s if abs((s := c.call("node.state", {"node": "attitude"}))["state"]["matrix"][0][0]) < .0001 else None))
        check(abs(attitude["state"]["matrix"][1][0]-1) < .0001, "degree Euler input rotates the aircraft by 90 degrees")
        check(c.call("node.state", {"node": "user"})["outputs"]["out"] == 6, "real local Python plugin imports bundled helper and computes output")
        logs = c.call("log.query", {"node": "user", "limit": 100})
        check("qa relative import" in json.dumps(logs), "Python print output appears in the native journal")
        try:
            action("ssh", "connect")
            raise AssertionError("Unknown host unexpectedly trusted")
        except RuntimeError as error:
            check("指纹" in str(error) or "信任" in str(error), "unknown SSH host requires explicit trust")
        c.call("node.configure", {"node": "ssh", "changes": {"allow_unknown_host": True}})
        action("ssh", "connect")
        until(lambda: "SSH 中文终端 ready" in str(action("terminal", "read_screen", styles=True)))
        action("terminal", "send", text="hello\r\n")
        until(lambda: b"hello\r\n" in server.received)
        screen = until(lambda: (s if "echo: hello" in str(s := action("terminal", "read_screen", styles=True)) else None))
        check("#" in screen["html"], "SSH authenticates and ANSI/UTF-8 shell round trip is styled")
        action("terminal", "resize", columns=100, rows=30)
        until(lambda: [100, 30] in server.sizes)
        check(True, "terminal dimensions reach the real SSH PTY")
        time.sleep(.4)
        c.call("capture", {"target": "runtime", "path": str(output / "runtime.png")})
        c.call("view.call", {"node": "plot", "view": "plot", "action": "export_image", "args": {"path": str(output / "plot.svg")}})
        check((output / "plot.svg").stat().st_size > 200, "native curve view exports SVG")
        c.call("project.save", {"path": str(output / "instruments.corig")})
        c.call("preferences.set", {"read_only": True})
        c.call("node.configure", {"node": "plot", "changes": {"line_width": 2.5}, "persist": False})
        check(not c.call("app.state")["dirty"], "read-only runtime tuning leaves saved design clean")
        check(c.call("node.state", {"node": "plot"})["config"]["line_width"] == 2.5, "runtime override takes effect")
        c.call("runtime.stop")
        check(c.call("node.state", {"node": "plot"})["config"]["line_width"] == 2.5, "ending a session retains current experiment parameters")
        c.call("runtime.reset")
        check(c.call("node.state", {"node": "plot"})["config"]["line_width"] == 1, "reset restores saved design after temporary runtime tuning")
        c.call("preferences.set", {"read_only": False})
        c.call("project.open", {"path": str(output / "instruments.corig")})
        c.call("runtime.start")
        check(c.call("node.state", {"node": "user"})["outputs"]["out"] == 6, "saved project reopens with Python helper resources intact")
        time.sleep(.5)
        controls = c.call("controls.list")
        check(any(item["id"] == "qa.user.label" and "6" in item.get("text", "") for item in controls), "packaged QML view renders live Python state")
        c.call("runtime.stop")
        c.call("project.save")
        (output / "report.json").write_text(json.dumps({"checks": checks, "count": len(checks)}, ensure_ascii=False, indent=2), "utf-8")
    finally:
        server.close()


if __name__ == "__main__":
    main()
