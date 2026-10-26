"""Exercise the actual GUI process over its API with real localhost transports.

All projects, frames and captures are test artifacts, not firmware/hardware
verification. The harness owns and closes only the application process it starts.
"""

from __future__ import annotations

import argparse
import base64
import json
import math
import os
import shutil
import socket
import subprocess
import sys
import threading
import time
import traceback
from pathlib import Path
from urllib.request import Request, urlopen
from urllib.error import HTTPError

ROOT = Path(__file__).resolve().parent.parent


class Client:
    def __init__(self, directory):
        self.info = json.loads((directory / "api.json").read_text(encoding="utf-8"))
        self.headers = {"Authorization": "Bearer " + self.info["token"],
                        "Content-Type": "application/json"}

    def command(self, op, **args):
        data = json.dumps({"op": op, "args": args}, ensure_ascii=False).encode("utf-8")
        request = Request(self.info["url"] + "/v1/command", data=data,
                          headers=self.headers, method="POST")
        try:
            with urlopen(request, timeout=25) as response:
                payload = json.load(response)
        except HTTPError as exc:
            raise RuntimeError(f"{op}: {exc.read().decode('utf-8')}") from exc
        if not payload["ok"]:
            raise RuntimeError(payload["error"])
        return payload["result"]

    def action(self, ident, name, **args):
        job = self.command("node.action", id=ident, action=name, args=args)["job_id"]
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            state = self.command("job.get", id=job)
            if state["status"] == "failed":
                raise RuntimeError(state["error"])
            if state["status"] == "complete":
                return state["result"]
            time.sleep(0.02)
        raise TimeoutError(name)

    def wait_for(self, predicate, timeout=8):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            state = self.command("app.state")
            if predicate(state):
                return state
            time.sleep(0.05)
        raise AssertionError(f"Condition not met: {state}")


class Echo:
    """A real TCP/UDP echo peer, bound to loopback on an OS-assigned port."""

    def __init__(self, udp=False):
        self.udp = udp
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM if udp else socket.SOCK_STREAM)
        self.socket.bind(("127.0.0.1", 0))
        self.port = self.socket.getsockname()[1]
        self.socket.settimeout(0.1)
        if not udp:
            self.socket.listen()
        self.stop = threading.Event()
        self.connections = []
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.stop.is_set():
            try:
                if self.udp:
                    data, address = self.socket.recvfrom(65536)
                    self.socket.sendto(data, address)
                else:
                    connection, _ = self.socket.accept()
                    self.connections.append(connection)
                    threading.Thread(target=self.peer, args=(connection,), daemon=True).start()
            except socket.timeout:
                continue
            except OSError:
                break

    def peer(self, connection):
        connection.settimeout(0.1)
        while not self.stop.is_set():
            try:
                data = connection.recv(65536)
                if not data:
                    return
                connection.sendall(data)
            except socket.timeout:
                continue
            except OSError:
                return

    def close(self):
        self.stop.set()
        self.socket.close()
        for connection in self.connections:
            connection.close()
        self.thread.join(timeout=1)


class SshPeer:
    """An actual local SSH protocol server with password auth and PTY resizing."""

    def __init__(self):
        import paramiko
        self.socket = socket.socket()
        self.socket.bind(("127.0.0.1", 0))
        self.socket.listen()
        self.socket.settimeout(0.1)
        self.port = self.socket.getsockname()[1]
        self.stop = threading.Event()
        self.transport = None
        self.size = None
        self.errors = []
        outer = self
        class Server(paramiko.ServerInterface):
            def check_auth_password(self, username, password):
                return paramiko.AUTH_SUCCESSFUL if (username, password) == ("test", "test") else paramiko.AUTH_FAILED
            def get_allowed_auths(self, username):
                return "password"
            def check_channel_request(self, kind, channel_id):
                return paramiko.OPEN_SUCCEEDED if kind == "session" else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED
            def check_channel_pty_request(self, channel, term, width, height, *args):
                outer.size = [width, height]
                return True
            def check_channel_shell_request(self, channel):
                return True
            def check_channel_window_change_request(self, channel, width, height, *args):
                outer.size = [width, height]
                return True
        key = paramiko.RSAKey.generate(2048)
        def run():
            try:
                while not self.stop.is_set():
                    try:
                        connection, _ = self.socket.accept()
                        break
                    except socket.timeout:
                        continue
                else:
                    return
                self.transport = paramiko.Transport(connection)
                self.transport.add_server_key(key)
                self.transport.start_server(server=Server())
                channel = self.transport.accept(5)
                if channel is None:
                    raise RuntimeError("No SSH channel")
                channel.settimeout(.1)
                channel.sendall(b"\x1b[32mSSH ready\x1b[0m\r\n")
                while not self.stop.is_set():
                    try:
                        data = channel.recv(65536)
                        if not data:
                            return
                        channel.sendall(data)
                    except socket.timeout:
                        continue
            except Exception as exc:
                if not self.stop.is_set():
                    self.errors.append(str(exc))
        self.thread = threading.Thread(target=run, daemon=True)
        self.thread.start()

    def close(self):
        self.stop.set()
        self.socket.close()
        if self.transport:
            self.transport.close()
        self.thread.join(timeout=1)


def node(client, ident, plugin, **config):
    return client.command("graph.node.add", id=ident, plugin=plugin, config=config)


def wire(client, source, port, target, input_port):
    return client.command("graph.wire.add", source={"node": source, "port": port},
                          target={"node": target, "port": input_port})


def exercise(client, artifacts):
    report = {}
    try:
        urlopen(client.info["url"] + "/v1/state", timeout=2)
        raise AssertionError("Unauthenticated state request was accepted")
    except HTTPError as exc:
        assert exc.code == 401
    report["authentication"] = "unauthenticated state rejected"
    initial = client.command("app.state")
    assert initial["mode"] == "runtime" and not initial["runtime"]["running"]
    assert initial["path"].endswith("SerialPlot.comtoolx")
    report["startup_project"] = "command-line project restored readonly and disconnected"
    registry = client.command("registry")
    assert len(registry["nodes"]) == 19 and registry["external"] == []
    report["default_catalog"] = "generic built-ins only; no project-specific plugin loaded"
    client.command("project.new", name="API 端到端验证")
    echo = Echo()
    try:
        node(client, "net", "io.network", port=echo.port)
        node(client, "raw", "debug.console")
        node(client, "decode", "protocol.plot")
        node(client, "plot", "plot.curves", max_points=10000)
        wire(client, "raw", "tx", "net", "tx")
        wire(client, "net", "rx", "raw", "rx")
        wire(client, "raw", "through", "decode", "bytes")
        wire(client, "decode", "samples", "plot", "samples")
        update = client.command("ui.update")
        assert len(update["added"]) == 3
        project = client.command("project.get")
        plot_item = next(item["id"] for item in project["ui"]["items"] if item.get("node") == "plot")
        raw_item = next(item["id"] for item in project["ui"]["items"] if item.get("node") == "raw")
        net_item = next(item["id"] for item in project["ui"]["items"] if item.get("node") == "net")
        client.command("ui.item.add", id="left", plugin="layout.column", parent="root", title="连接",
                       stretch=1, properties={"max_width": 270})
        client.command("ui.item.add", id="main", plugin="layout.tabs", parent="root", title="", stretch=4)
        client.command("ui.item.move", id=net_item, parent="left")
        client.command("ui.item.move", id=plot_item, parent="main")
        client.command("ui.item.move", id=raw_item, parent="main")
        client.command("ui.item.remove", id="pending")
        client.command("ui.item.patch", id=plot_item, title="原生实时曲线")
        assert client.command("ui.update")["added"] == []
        client.command("workspace.set", view="graph")
        client.command("graph.fit")
        report["graph_capture"] = client.command("capture", target="graph", width=1800)
        report["graph_window_capture"] = client.command("capture", target="window")
        report["layout_capture"] = client.command("capture", target="layout")
        client.command("runtime.start")
        client.action("net", "connect")
        frames = "".join(f"$sin,{index / 1000:.3f},{math.sin(index / 80):.8f}\n"
                         for index in range(2000))
        result = client.action("raw", "send", text=frames)
        assert result["delivery"]["sent"] == len(frames.encode())
        state = client.wait_for(lambda state: state["runtime"]["nodes"]["plot"]["points"] == 2000)
        assert state["runtime"]["failed"] == 0, state["runtime"]["events"]
        data = client.action("plot", "data", channel="sin", limit=3)
        assert data["sin"]["total"] == 2000
        client.command("workspace.set", view="runtime")
        report["runtime_capture"] = client.command("capture", target="runtime")
        report["runtime_window_capture"] = client.command("capture", target="window")
        client.command("view.call", id="runtime/" + plot_item, action="export_image",
                       args={"path": str(artifacts / "plot.svg")})
        client.command("view.call", id="runtime/" + plot_item, action="export_image",
                       args={"path": str(artifacts / "plot.png"), "width": 1200})
        client.action("plot", "export_csv", path=str(artifacts / "plot.csv"))
        assert (artifacts / "plot.svg").stat().st_size > 1000
        assert len((artifacts / "plot.csv").read_text(encoding="utf-8-sig").splitlines()) == 2001
        controls = client.command("controls.list", root="runtime/" + plot_item)
        assert any("plot.pause" in item["id"] for item in controls)
        client.action("plot", "pause", value=True)
        client.action("raw", "send", text="$sin,2,0.5\n")
        client.wait_for(lambda state: state["runtime"]["nodes"]["plot"]["points"] == 2001)
        assert client.command("app.state")["runtime"]["nodes"]["plot"]["paused"] is True
        client.action("plot", "pause", value=False)
        report["tcp_pipeline"] = {"decoded_points": 2001, "failed": 0, "exported": ["csv", "svg", "png"]}
        try:
            node(client, "forbidden", "control.button")
            raise AssertionError("Running graph accepted topology editing")
        except RuntimeError as exc:
            assert "停止" in str(exc)
        client.command("runtime.stop")
        saved_path = artifacts / "ApiDesigned.comtoolx"
        client.command("project.save", path=str(saved_path))
        client.command("project.open", path=str(saved_path), read_only=True)
        assert not client.command("app.state")["runtime"]["running"]
        try:
            client.command("ui.item.patch", id=plot_item, title="forbidden")
            raise AssertionError("Readonly design was editable")
        except RuntimeError as exc:
            assert "只读" in str(exc)
        report["save_and_readonly"] = "both designs preserved; IO remains stopped"
    finally:
        client.command("runtime.stop")
        echo.close()
    # UDP uses the same real IO contract, not a substituted test runtime.
    client.command("project.new", name="UDP 验证")
    echo = Echo(udp=True)
    try:
        node(client, "udp", "io.network", protocol="udp", port=echo.port)
        node(client, "debug", "debug.console")
        wire(client, "debug", "tx", "udp", "tx")
        wire(client, "udp", "rx", "debug", "rx")
        client.command("runtime.start")
        client.action("udp", "connect")
        client.action("debug", "send", text="UDP actual echo")
        client.wait_for(lambda state: state["runtime"]["nodes"]["debug"]["rx_bytes"] > 0)
        raw = client.action("debug", "read")
        assert base64.b64decode(raw["chunks"][0]["data"]["base64"]) == b"UDP actual echo"
        report["udp"] = "real loopback datagram echoed"
    finally:
        client.command("runtime.stop")
        echo.close()
    # Exercise the official serial/plot example with the real software loopback.
    client.command("project.open", path=str(ROOT / "examples" / "SerialPlot.comtoolx"))
    client.command("graph.node.patch", id="serial", config={"port": "loop://"})
    client.command("runtime.start")
    client.action("serial", "connect")
    result = client.action("serial", "send", text="$signal,1,2\n")
    assert result["sent"] > 0
    client.wait_for(lambda state: state["runtime"]["nodes"]["plot"]["points"] == 1)
    report["serial_plot_example"] = "official serial/plot project decoded a real loopback frame"
    client.command("runtime.stop")
    # Test both server modes with a separate actual socket client.
    for udp in (False, True):
        client.command("project.new", name="服务端验证")
        node(client, "server", "io.network", protocol="udp" if udp else "tcp", mode="server",
             port=23456)
        # Find an unused port before configuring the real server.
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM if udp else socket.SOCK_STREAM) as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        client.command("graph.node.patch", id="server", config={"port": port})
        node(client, "console", "debug.console")
        wire(client, "server", "rx", "console", "rx")
        wire(client, "console", "tx", "server", "tx")
        client.command("runtime.start")
        client.action("server", "connect")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM if udp else socket.SOCK_STREAM) as peer:
            peer.settimeout(3)
            if udp:
                peer.sendto(b"server input", ("127.0.0.1", port))
            else:
                peer.connect(("127.0.0.1", port))
                peer.sendall(b"server input")
            client.wait_for(lambda state: state["runtime"]["nodes"]["console"]["rx_bytes"] == 12)
            client.action("console", "send", text="server response")
            assert peer.recv(100) == b"server response"
        client.command("runtime.stop")
    report["network_servers"] = "TCP and UDP received and replied through actual sockets"
    client.command("project.new", name="SSH 终端验证")
    ssh = SshPeer()
    try:
        node(client, "ssh", "io.ssh", port=ssh.port, user="test", password="test", allow_unknown_host=True)
        node(client, "terminal", "terminal.ansi")
        wire(client, "ssh", "rx", "terminal", "rx")
        wire(client, "terminal", "tx", "ssh", "tx")
        wire(client, "terminal", "resize", "ssh", "resize")
        client.command("ui.update")
        client.command("runtime.start")
        client.action("ssh", "connect")
        client.action("terminal", "send", text="SSH actual echo\r\n")
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            screen = client.action("terminal", "read_screen")
            if "SSH actual echo" in "\n".join(screen["lines"]):
                break
            time.sleep(.05)
        else:
            raise AssertionError(screen)
        client.action("terminal", "resize", columns=100, rows=30)
        deadline = time.monotonic() + 2
        while ssh.size != [100, 30] and time.monotonic() < deadline:
            time.sleep(.02)
        assert ssh.size == [100, 30], ssh.size
        report["ssh"] = "actual password handshake, PTY, ANSI terminal, echoed input and resize"
    finally:
        client.command("runtime.stop")
        ssh.close()
    # Exercise extension registration, view discovery and standard GUI controls.
    client.command("project.new", name="外部插件与控件验证")
    plugin_source = artifacts / "extension-source/gain.py"
    plugin_source.parent.mkdir(exist_ok=True)
    shutil.copy2(ROOT / "docs/plugin_examples/gain.py", plugin_source)
    client.command("plugin.load", path=str(plugin_source))
    node(client, "input", "control.number")
    node(client, "gain", "example.gain", gain=3)
    node(client, "display", "display.value")
    node(client, "check", "control.checkbox")
    node(client, "boolean", "display.value")
    wire(client, "input", "value", "gain", "in")
    wire(client, "gain", "value", "display", "value")
    wire(client, "check", "value", "boolean", "value")
    client.command("ui.update")
    project = client.command("project.get")
    item = next(entry for entry in project["ui"]["items"] if entry.get("node") == "input")
    client.command("workspace.set", view="runtime")
    client.command("runtime.start")
    control = next(entry for entry in client.command("controls.list", root="runtime/" + item["id"])
                   if entry["class"] == "QDoubleSpinBox")
    client.command("controls.set", id=control["id"], value=4)
    client.wait_for(lambda state: state["runtime"]["nodes"]["display"].get("value") == 12)
    checkbox = next(entry for entry in client.command("controls.list")
                    if entry["class"] == "QCheckBox" and "/check.value" in entry["id"])
    client.command("controls.click", id=checkbox["id"])
    client.wait_for(lambda state: state["runtime"]["nodes"]["boolean"].get("value") is True)
    client.command("capture", target="runtime")
    report["external_plugin_and_controls"] = "loaded external gain; API edited real spinbox; 4 * 3 reached display"
    client.command("runtime.stop")
    client.command("graph.node.patch", id="gain", rotation=90)
    route_id = client.command("project.get")["wires"][0]["id"]
    route_points = [[300, 250], [500, 250]]
    client.command("graph.wire.patch", id=route_id, properties={"points": route_points, "style": "straight"})
    rotated = artifacts / "Rotated.comtoolx"
    client.command("project.save", path=str(rotated))
    client.command("project.close")
    assert client.command("registry")["external"] == []
    plugin_source.replace(plugin_source.with_suffix(".disabled"))
    client.command("project.open", path=str(rotated))
    gain = next(entry for entry in client.command("project.get")["nodes"] if entry["id"] == "gain")
    assert gain["rotation"] == 90
    assert client.command("project.get")["wires"][0]["properties"]["points"] == route_points
    report["wire_route"] = "editable path coordinates persisted through save/open"
    report["rotation"] = "API orientation persisted through save/open"
    report["rotation_capture"] = client.command("capture", target="graph", width=1800)
    client.command("runtime.start")
    client.action("input", "set_value", value=2)
    client.wait_for(lambda state: state["runtime"]["nodes"]["display"].get("value") == 6)
    client.command("runtime.stop")
    report["portable_project"] = "single .comtoolx reopened without original extension source; packaged code produced 2 * 3 = 6"
    client.command("project.new", name="四方向文字排版验证")
    assert client.command("registry")["external"] == []
    assert not any(item["id"] == "example.gain" for item in client.command("registry")["nodes"])
    for angle, position in ((0, [40, 110]), (90, [410, 110]),
                            (180, [40, 450]), (270, [410, 450])):
        client.command("graph.node.add", id=f"angle{angle}", plugin="io.serial",
                       name=f"串口 {angle}°", rotation=angle, position=position)
    assert client.command("graph.overlaps")["overlaps"] == []
    client.command("graph.node.add", id="near", plugin="display.value", position=[80, 260])
    assert client.command("graph.overlaps")["overlaps"] == []
    client.command("graph.node.patch", id="angle0", rotation=90)
    pairs = client.command("graph.overlaps")["overlaps"]
    assert any(set(pair["nodes"]) == {"angle0", "near"} for pair in pairs), pairs
    client.command("graph.node.move", id="near", position=[800, 450])
    assert client.command("graph.overlaps")["overlaps"] == []
    client.command("graph.node.remove", id="near")
    client.command("graph.node.patch", id="angle0", rotation=0)
    report["text_rotation_capture"] = client.command("capture", target="graph", width=1200)
    report["rotated_occupancy"] = "rotated extents detected overlap; moving apart cleared warning"
    before = client.command("project.get")
    preview = client.command("library.preview", plugin="protocol.plot")
    assert {port["id"] for port in preview["ports"]} == {"bytes", "samples"}
    report["library_preview_capture"] = client.command("capture", target=preview["target"])
    assert client.command("project.get") == before
    report["library_preview"] = "real symbol and ports rendered without adding runtime nodes"
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path)
    options = parser.parse_args()
    artifacts = ROOT / "verification" / ("packaged" if options.executable else "source")
    artifacts.mkdir(parents=True, exist_ok=True)
    data = artifacts / "session"
    data.mkdir(exist_ok=True)
    environment = os.environ.copy()
    environment["QT_QPA_PLATFORM"] = "offscreen"
    if options.executable:
        environment["PATH"] = str(Path(os.environ["SystemRoot"]) / "System32")
        environment.pop("PYTHONPATH", None)
        environment.pop("PYTHONHOME", None)
    args = [str(options.executable)] if options.executable else [sys.executable, "-m", "comtoolx"]
    args += ["--data-dir", str(data), "--project", str(ROOT / "examples/SerialPlot.comtoolx"),
             "--read-only"]
    log = (artifacts / "process.log").open("w", encoding="utf-8")
    process = subprocess.Popen(args, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT)
    client = None
    try:
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError((artifacts / "process.log").read_text(encoding="utf-8"))
            info = data / "api.json"
            if info.exists():
                discovered = json.loads(info.read_text(encoding="utf-8"))
                # Windows venv python.exe may be a launcher for the actual interpreter.
                if process.pid in (discovered.get("pid"), discovered.get("parent_pid")):
                    client = Client(data)
                    break
            time.sleep(0.1)
        if client is None:
            raise TimeoutError("Application did not publish API discovery")
        cli_report = None
        if options.executable:
            executable = options.executable.resolve().with_name("COMTool-x-cli.exe")
            result = subprocess.run([str(executable), "api", "app.state", "--data-dir", str(data)],
                                    capture_output=True, encoding="utf-8", timeout=15, env=environment)
            assert result.returncode == 0, result.stderr
            assert json.loads(result.stdout)["ok"]
            request_file = artifacts / "cli-request.json"
            request_file.write_text(json.dumps({"id": "plot", "action": "pause",
                                                "args": {"value": False}}), encoding="utf-8")
            result = subprocess.run([str(executable), "api", "node.action", "--wait",
                                    "--json-file", str(request_file), "--data-dir", str(data)],
                                   capture_output=True, encoding="utf-8", timeout=15, env=environment)
            assert result.returncode == 0, result.stderr
            assert json.loads(result.stdout)["result"]["status"] == "complete", result.stdout
            cli_report = "standalone CLI state, JSON file and asynchronous --wait passed"
        report = exercise(client, artifacts)
        if cli_report:
            report["packaged_cli"] = cli_report
        report.update(status="passed", hardware_validation=False, pid=process.pid)
        print(json.dumps(report, ensure_ascii=False, indent=2))
        (artifacts / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    finally:
        if client and process.poll() is None:
            try:
                client.command("app.quit", discard=True)
                process.wait(timeout=10)
            except Exception:
                process.terminate()
                process.wait(timeout=5)
        elif process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
        log.close()


if __name__ == "__main__":
    main()
