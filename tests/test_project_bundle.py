"""Portable project behaviour with real source plugins, resources and graph execution."""

import json
import sys
import tempfile
import time
import unittest
import zipfile
from pathlib import Path

from PySide6.QtCore import QTimer
from PySide6.QtWidgets import QApplication, QFileDialog, QLineEdit

from comtoolx.controller import Controller
from comtoolx.model import load_project
from comtoolx.plugins import register_builtins
from comtoolx.sdk import Registry, WorkbenchError
from comtoolx.ui.window import MainWindow

APP = QApplication.instance() or QApplication([])

PLUGIN = '''from pathlib import Path
from comtoolx.sdk import Definition, NodeRuntime, Port
from .helper import FACTOR
PROJECT_FILES = ["assets/label.txt"]
class Gain(NodeRuntime):
    def __init__(self, node, engine):
        super().__init__(node, engine)
        self.state["label"] = (Path(__file__).parent / "assets/label.txt").read_text()
        self.state["calibration"] = Path(self.config["calibration_file"]).read_text()
    def receive(self, port, data):
        return self.emit("out", data * self.config["gain"] * FACTOR)
def register(registry):
    registry.register(Definition("example.portable", "Portable gain", "Project", "Gain with resources",
        (Port("in", "In", "in", "number"), Port("out", "Out", "out", "number")),
        {"gain": {"type": "number", "default": 1},
         "calibration_file": {"type": "string", "default": "", "format": "file"}}, Gain))
'''


class BundleTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.controllers = []
        self.c = self.controller()

    def tearDown(self):
        for controller in self.controllers:
            controller.close()
        self.temporary.cleanup()

    def controller(self):
        registry = Registry()
        register_builtins(registry)
        controller = Controller(registry, self.root / f"session-{len(self.controllers)}", self.root / "project")
        self.controllers.append(controller)
        return controller

    def create_bundle(self, name, factor=2):
        """Build a working graph from a provider that depends on both helper code and files."""
        folder = self.root / name
        (folder / "assets").mkdir(parents=True)
        (folder / "entry.py").write_text(PLUGIN, encoding="utf-8")
        (folder / "helper.py").write_text(f"FACTOR = {factor}\n", encoding="utf-8")
        (folder / "assets/label.txt").write_text(name, encoding="utf-8")
        calibration = folder / "calibration.txt"
        calibration.write_text("calibrated", encoding="utf-8")
        self.c.command("project.new", {"name": name})
        self.c.command("plugin.load", {"path": str(folder / "entry.py")})
        for ident, plugin, config in (("input", "control.number", {}),
                ("gain", "example.portable", {"gain": 3, "calibration_file": str(calibration)}),
                ("display", "display.value", {})):
            self.c.command("graph.node.add", {"id": ident, "plugin": plugin, "config": config})
        for source, port, target, target_port in (("input", "value", "gain", "in"), ("gain", "out", "display", "value")):
            self.c.command("graph.wire.add", {"source": {"node": source, "port": port},
                                            "target": {"node": target, "port": target_port}})
        self.c.command("ui.update")
        self.c.command("graph.node.patch", {"id": "gain", "rotation": 270})
        # The saved implementation must be the imported snapshot, not later edits on disk.
        (folder / "helper.py").write_text("FACTOR = 999\n", encoding="utf-8")
        result = self.c.command("project.save")
        return Path(result["path"]), folder

    def run_gain(self, controller, expected):
        controller.command("runtime.start")
        controller.command("node.action", {"id": "input", "action": "set_value", "args": {"value": 4}})
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            if controller.engine.nodes["display"].snapshot().get("value") == expected:
                break
            time.sleep(.01)
        self.assertEqual(expected, controller.engine.nodes["display"].snapshot()["value"])
        controller.command("runtime.stop")

    def test_one_file_reopens_without_sources_and_resaves_after_original_is_removed(self):
        path, folder = self.create_bundle("shared")
        self.assertEqual(self.root / "project/shared.comtoolx", path)
        with zipfile.ZipFile(path) as archive:
            manifest = json.loads(archive.read("manifest.json"))
            self.assertEqual(1, sum(not provider["builtin"] for provider in manifest["providers"]))
            self.assertTrue(any(name.endswith("/helper.py") for name in archive.namelist()))
        folder.rename(self.root / "sources-not-available")
        recipient = self.controller()
        recipient.command("project.open", {"path": str(path)})
        self.assertFalse(recipient.engine.running)
        self.assertEqual("shared", recipient.engine.nodes["gain"].snapshot()["label"])
        self.assertEqual("calibrated", recipient.engine.nodes["gain"].snapshot()["calibration"])
        self.assertEqual(270, next(node for node in recipient.project["nodes"] if node["id"] == "gain")["rotation"])
        self.assertTrue(recipient.registry.nodes["control.number"].factory.__module__.startswith("comtoolx._project_"))
        self.run_gain(recipient, 24)
        second = self.root / "resaved.comtoolx"
        recipient.command("project.save", {"path": str(second)})
        recipient.command("project.close")
        recipient.command("project.open", {"path": str(second)})
        self.run_gain(recipient, 24)

    def test_switch_and_close_remove_previous_plugins_modules_and_resources(self):
        first, _ = self.create_bundle("first", 2)
        second, _ = self.create_bundle("second", 5)
        self.c.command("project.open", {"path": str(first)})
        old_root = self.c.contents.root
        namespaces = self.c.contents.namespaces.copy()
        self.run_gain(self.c, 24)
        self.c.command("project.open", {"path": str(second)})
        self.assertFalse(old_root.exists())
        self.assertFalse(any(name == prefix or name.startswith(prefix + ".") for prefix in namespaces for name in sys.modules))
        self.assertEqual("second", self.c.engine.nodes["gain"].snapshot()["label"])
        self.run_gain(self.c, 60)
        self.c.command("project.close")
        self.assertNotIn("example.portable", self.c.registry.nodes)
        self.assertEqual([], self.c.registry.external)
        self.assertEqual([], self.c.project["nodes"])

    def test_unused_imports_are_not_packaged_and_failed_save_preserves_existing_file(self):
        path, folder = self.create_bundle("keep")
        before = path.read_bytes()
        (folder / "calibration.txt").unlink()
        with self.assertRaisesRegex(WorkbenchError, "资源不存在"):
            self.c.command("project.save")
        self.assertEqual(before, path.read_bytes())
        self.c.command("graph.node.remove", {"id": "gain"})
        self.c.command("project.save", {"path": str(self.root / "without-gain.comtoolx")})
        with zipfile.ZipFile(self.root / "without-gain.comtoolx") as archive:
            self.assertTrue(all(provider["builtin"] for provider in json.loads(archive.read("manifest.json"))["providers"]))

    def test_json_and_invalid_archives_are_rejected_without_replacing_current_design(self):
        self.c.command("project.new", {"name": "keep active"})
        before = self.c.command("project.get")
        legacy = self.root / "old.json"
        legacy.write_text(json.dumps(before))
        with self.assertRaisesRegex(WorkbenchError, "不支持 JSON"):
            self.c.command("project.open", {"path": str(legacy)})
        with self.assertRaisesRegex(WorkbenchError, "保存为 .comtoolx"):
            self.c.command("project.save", {"path": str(legacy)})
        unsafe = self.root / "unsafe.comtoolx"
        with zipfile.ZipFile(unsafe, "w") as archive:
            archive.writestr("../escape.txt", "unsafe")
        with self.assertRaisesRegex(WorkbenchError, "不安全"):
            self.c.command("project.open", {"path": str(unsafe)})
        self.assertEqual(before, self.c.command("project.get"))
        self.assertFalse((self.root / "escape.txt").exists())

    def test_save_dialog_uses_project_folder_and_appends_project_suffix(self):
        window = MainWindow(self.c)
        window.show()
        seen = {}
        def choose_filename():
            dialog = next(widget for widget in APP.topLevelWidgets() if isinstance(widget, QFileDialog) and widget.isVisible())
            seen["directory"] = dialog.directory().absolutePath()
            seen["filters"] = dialog.nameFilters()
            editor = dialog.findChild(QLineEdit, "fileNameEdit")
            if editor is None:
                dialog.reject()
                return
            editor.setText("WithoutSuffix")
            dialog.accept()
        QTimer.singleShot(100, choose_filename)
        try:
            self.assertTrue(window.save_project(False))
            self.assertEqual(str(self.c.project_dir / "WithoutSuffix.comtoolx"), self.c.path)
            self.assertEqual(self.c.project_dir, Path(seen["directory"]))
            self.assertEqual(["COMTool-x 工程 (*.comtoolx)"], seen["filters"])
            self.assertTrue(zipfile.is_zipfile(self.c.path))
        finally:
            window.discard_on_close = True
            window.close()


if __name__ == "__main__":
    unittest.main()
