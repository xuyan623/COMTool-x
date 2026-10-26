"""Behavioural checks for graph transactions, IO, frame parsing and UI persistence."""

from __future__ import annotations

import json
import struct
import tempfile
import time
import unittest
from pathlib import Path

from PySide6.QtWidgets import QApplication

from comtoolx.sdk import Registry, WorkbenchError
from comtoolx.plugins import register_builtins
from comtoolx.controller import Controller
from comtoolx.model import load_project, validate
from comtoolx.plugins.plotting import SeriesBuffer
from comtoolx.plugins.protocols import encode_text

ROOT = Path(__file__).resolve().parent.parent
APP = QApplication.instance() or QApplication([])


class CoreTests(unittest.TestCase):
    def setUp(self):
        (ROOT / ".sessions").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=ROOT / ".sessions")
        registry = Registry()
        register_builtins(registry)
        self.c = Controller(registry, self.temporary.name)

    def tearDown(self):
        self.c.close()
        self.temporary.cleanup()

    def add(self, ident, plugin, **config):
        return self.c.command("graph.node.add", {"id": ident, "plugin": plugin, "config": config})

    def connect(self, source, port, target, input_port):
        return self.c.command("graph.wire.add", {
            "source": {"node": source, "port": port},
            "target": {"node": target, "port": input_port}})

    def action(self, ident, action, args=None):
        job = self.c.command("node.action", {"id": ident, "action": action, "args": args or {}})
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            state = self.c.engine.job(job["job_id"])
            if state["status"] == "failed":
                raise WorkbenchError(state["error"])
            if state["status"] == "complete":
                return state["result"]
            time.sleep(0.01)
        self.fail("Action did not complete")

    def test_types_occupancy_and_transaction_rollback(self):
        self.add("a", "control.number")
        self.add("b", "display.value")
        self.add("c", "io.serial")
        before = self.c.command("project.get")
        with self.assertRaises(WorkbenchError):
            self.connect("a", "value", "c", "tx")
        self.assertEqual(before, self.c.project)
        self.assertEqual(self.c.engine.project, self.c.project)
        self.connect("a", "value", "b", "value")
        with self.assertRaises(WorkbenchError):
            self.connect("a", "value", "b", "value")
        self.c.command("runtime.start")
        self.action("a", "set_value", {"value": 12.5})
        self.assertEqual(12.5, self.c.engine.nodes["b"].snapshot()["value"])

    def test_readonly_running_and_explicit_ui_update(self):
        self.add("a", "control.number")
        self.assertEqual(1, len(self.c.project["ui"]["items"]))
        result = self.c.command("ui.update")
        self.assertEqual(1, len(result["added"]))
        ident = result["added"][0]
        self.c.command("ui.item.patch", {"id": ident, "title": "My panel", "stretch": 3})
        self.c.command("ui.update")
        item = next(item for item in self.c.project["ui"]["items"] if item["id"] == ident)
        self.assertEqual(("My panel", 3), (item["title"], item["stretch"]))
        self.c.command("ui.item.remove", {"id": ident})
        self.assertEqual([], self.c.command("ui.update")["added"])
        self.c.command("mode.set", {"mode": "runtime"})
        with self.assertRaises(WorkbenchError):
            self.add("b", "control.button")
        self.c.command("runtime.start")
        self.action("a", "set_value", {"value": 3})
        self.c.command("mode.set", {"mode": "edit"})
        with self.assertRaises(WorkbenchError):
            self.add("b", "control.button")

    def test_save_open_is_disconnected_and_keeps_both_designs(self):
        self.add("s", "io.serial", port="loop://")
        self.c.command("ui.update")
        target = str(Path(self.temporary.name) / "project.comtoolx")
        self.c.command("project.save", {"path": target})
        saved = load_project(target)
        self.c.command("project.open", {"path": target, "read_only": True})
        self.assertEqual(saved, self.c.project)
        self.assertFalse(self.c.engine.running)
        self.assertFalse(self.c.engine.nodes["s"].snapshot()["connected"])
        self.assertTrue(self.c.design_locked)

    def test_loopback_serial_to_real_parser_and_native_plot_runtime(self):
        self.add("serial", "io.serial", port="loop://")
        self.add("send", "debug.console")
        self.add("decode", "protocol.plot")
        self.add("plot", "plot.curves", max_points=100)
        self.connect("send", "tx", "serial", "tx")
        self.connect("serial", "rx", "decode", "bytes")
        self.connect("decode", "samples", "plot", "samples")
        self.c.command("runtime.start")
        self.action("serial", "connect")
        self.action("send", "send", {"text": "$x,1,2\n$x,2,3\n"})
        deadline = time.monotonic() + 3
        while self.c.engine.nodes["plot"].snapshot()["points"] < 2 and time.monotonic() < deadline:
            time.sleep(0.01)
        data = self.action("plot", "data")
        self.assertEqual([2, 3], data["x"]["y"])
        state = self.c.engine.nodes["serial"].snapshot()
        previous = state["last_tx_hex"]
        self.action("serial", "disconnect")
        with self.assertRaises(WorkbenchError):
            self.action("serial", "send", {"text": "failed"})
        self.assertEqual(previous, self.c.engine.nodes["serial"].snapshot()["last_tx_hex"])

    def test_ascii_and_binary_fragmentation_checksum(self):
        self.add("decoder", "protocol.plot")
        self.add("plot", "plot.curves", max_points=100)
        self.connect("decoder", "samples", "plot", "samples")
        self.c.command("runtime.start")
        decoder = self.c.engine.nodes["decoder"]
        for data in (b"noise$one,0,", b"1\n$bad,2,3,0\n$two,1,4\r\n"):
            receipt = decoder.receive("bytes", data)
            if receipt:
                receipt.result(timeout=2)
        self.assertEqual(2, decoder.snapshot()["frames"])
        self.assertEqual(1, decoder.snapshot()["bad_frames"])
        self.c.command("runtime.stop")
        self.c.command("graph.node.patch", {"id": "decoder", "config": {"binary": True}})
        self.c.command("runtime.start")
        decoder = self.c.engine.nodes["decoder"]
        frame = b"\xAA\xCC\xEE\xBB\x01x" + struct.pack("<dd", 1.25, -0.5)
        frame += bytes([sum(frame) % 256])
        self.assertIsNone(decoder.receive("bytes", frame[:4]))
        decoder.receive("bytes", frame[4:]).result(timeout=2)
        self.assertEqual([-0.5], self.action("plot", "data", {"channel": "x"})["x"]["y"])

    def test_ring_history_and_text_encoding(self):
        import numpy as np
        buffer = SeriesBuffer(5)
        buffer.append([0, 1, 2], [0, 1, 2])
        buffer.append([3, 4, 5, 6], [3, 4, 5, 6])
        self.assertEqual([2, 3, 4, 5, 6], buffer.arrays()[0].tolist())
        self.assertEqual([4, 5, 6], buffer.arrays(window=1)[0].tolist())
        self.assertEqual("中".encode() + b"\xAA\r\n", encode_text(r"中\xAA", escape=True, newline="CRLF"))

    def test_default_catalog_and_official_example_are_generic(self):
        expected = {
            "control.button", "control.checkbox", "control.number", "control.select",
            "control.slider", "control.text", "control.timer", "data.recorder",
            "debug.console", "display.value", "io.network", "io.serial", "io.ssh",
            "plot.curves", "protocol.encode", "protocol.plot", "protocol.python",
            "protocol.text", "terminal.ansi"}
        self.assertEqual(expected, set(self.c.registry.nodes))
        self.assertEqual([], self.c.registry.external)
        self.assertEqual({"SerialPlot.comtoolx"},
                         {path.name for path in (ROOT / "examples").glob("*.comtoolx")})
        project = load_project(str(ROOT / "examples/SerialPlot.comtoolx"))
        self.assertEqual([], validate(project, self.c.registry))

    def test_python_codec_and_terminal_are_real_plugins(self):
        self.add("codec", "protocol.python", operation="encode",
                 code="def encode(data):\n    return data + pack('<H', crc16(data))\n")
        self.add("bytes", "debug.console")
        self.connect("codec", "out", "bytes", "rx")
        self.add("terminal", "terminal.ansi")
        self.c.command("runtime.start")
        self.c.engine.nodes["codec"].receive("in", b"123456789").result(timeout=2)
        retained = self.c.engine.nodes["bytes"].read_since(0)["chunks"]
        self.assertEqual(b"123456789\x37\x4b", retained[0][2])
        terminal = self.c.engine.nodes["terminal"]
        terminal.receive("rx", b"\x1b[31mred\x1b[0m\r\nnext")
        screen = self.action("terminal", "read_screen")
        self.assertTrue(screen["lines"][0].startswith("red"))
        self.assertTrue(screen["lines"][1].startswith("next"))
        self.assertEqual("red", terminal.screen.buffer[0][0].fg)

    def test_wire_and_ui_property_errors_leave_design_unchanged(self):
        self.add("a", "control.number")
        self.add("b", "display.value")
        connection = self.connect("a", "value", "b", "value")
        self.c.command("ui.update")
        before = self.c.command("project.get")
        for command in (
            ("graph.wire.patch", {"id": connection["id"], "properties": {"width": "bad"}}),
            ("graph.wire.patch", {"id": connection["id"], "properties": {"color": "bad"}}),
            ("ui.item.patch", {"id": "root", "properties": {"spacing": -1}}),
            ("ui.item.patch", {"id": "root", "properties": {"min_width": "bad"}}),
        ):
            with self.assertRaises(WorkbenchError):
                self.c.command(*command)
            self.assertEqual(before, self.c.project)

    def test_explicit_disconnect_can_reconnect_without_replaying_old_tx(self):
        self.add("s", "io.serial", port="loop://", auto_reconnect=True)
        self.c.command("runtime.start")
        for index in range(4):
            self.action("s", "connect")
            result = self.action("s", "send", {"text": str(index)})
            self.assertEqual(1, result["sent"])
            self.action("s", "disconnect")
            self.assertFalse(self.c.engine.nodes["s"].snapshot()["connected"])
            self.assertEqual("", self.c.engine.nodes["s"].snapshot()["error"])
        self.assertEqual(4, self.c.engine.nodes["s"].snapshot()["tx_bytes"])

    def test_parser_recovers_after_partial_old_frame_and_checksum_error(self):
        self.add("decoder", "protocol.plot")
        self.add("plot", "plot.curves", max_points=100)
        self.connect("decoder", "samples", "plot", "samples")
        self.c.command("runtime.start")
        decoder = self.c.engine.nodes["decoder"]
        decoder.receive("bytes", b"$old,1,")
        decoder.receive("bytes", b"$new,2,3\n").result(timeout=2)
        self.assertEqual([3], self.action("plot", "data")["new"]["y"])
        self.c.command("runtime.stop")
        self.c.command("graph.node.patch", {"id": "decoder", "config": {"binary": True}})
        self.c.command("runtime.start")
        decoder = self.c.engine.nodes["decoder"]
        frame = b"\xAA\xCC\xEE\xBB\x01x" + struct.pack("<dd", 2.0, 4.0)
        frame += bytes([sum(frame) % 256])
        decoder.receive("bytes", frame[:10])
        decoder.receive("bytes", frame).result(timeout=2)
        self.assertEqual([4], self.action("plot", "data")["x"]["y"])

    def test_debug_rejects_unconnected_send_and_clears_all_views(self):
        from comtoolx.plugins.basic import debug_view
        from PySide6.QtWidgets import QPlainTextEdit
        self.add("debug", "debug.console")
        self.c.command("runtime.start")
        with self.assertRaises(WorkbenchError):
            self.action("debug", "send", {"text": "not delivered"})
        runtime = self.c.engine.nodes["debug"]
        self.assertEqual(0, runtime.snapshot()["queued_tx_bytes"])
        widget = debug_view(runtime, lambda name, args: self.action("debug", name, args), {})
        display = widget.findChild(QPlainTextEdit)
        raw = "中文".encode()
        runtime.receive("rx", raw[:2])
        widget.refresh_from_runtime()
        runtime.receive("rx", raw[2:])
        widget.refresh_from_runtime()
        self.assertIn("中文", display.toPlainText())
        self.action("debug", "clear")
        widget.refresh_from_runtime()
        self.assertEqual("", display.toPlainText())
        widget.deleteLater()


if __name__ == "__main__":
    unittest.main()
