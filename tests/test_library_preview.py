"""Catalogue hover renders real symbols without adding nodes or opening devices."""

import copy
import tempfile
import unittest
from pathlib import Path

from PySide6.QtCore import Qt, QEvent
from PySide6.QtGui import QHelpEvent, QImage
from PySide6.QtWidgets import QApplication

from comtoolx.sdk import Registry
from comtoolx.plugins import register_builtins
from comtoolx.controller import Controller
from comtoolx.ui.window import MainWindow

APP = QApplication.instance() or QApplication([])
ROOT = Path(__file__).resolve().parent.parent


class LibraryPreviewTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=ROOT / ".sessions")
        registry = Registry()
        register_builtins(registry)
        self.c = Controller(registry, self.temporary.name)
        self.window = MainWindow(self.c)
        self.window.show()
        APP.processEvents()

    def tearDown(self):
        self.window.discard_on_close = True
        self.window.close()
        self.temporary.cleanup()

    def hover(self, item):
        library = self.window.library
        library.scrollToItem(item)
        point = library.visualItemRect(item).center()
        event = QHelpEvent(QEvent.Type.ToolTip, point, library.viewport().mapToGlobal(point))
        APP.sendEvent(library.viewport(), event)

    def test_native_tooltip_event_shows_real_ports_without_modifying_project(self):
        library = self.window.library
        item = library.findItems("曲线协议解码",
            Qt.MatchFlag.MatchExactly | Qt.MatchFlag.MatchRecursive)[0]
        before = copy.deepcopy(self.c.project)
        self.hover(item)
        self.assertTrue(library.preview.isVisible())
        self.assertEqual("protocol.plot", library.preview.plugin_id)
        expected = {port.id for port in self.c.registry.nodes["protocol.plot"].ports}
        self.assertEqual(expected, set(library.preview.node.ports))
        self.assertEqual(before, self.c.project)
        self.assertEqual({}, self.c.engine.nodes)
        self.assertFalse(self.c.engine.running)
        self.assertFalse(self.c.dirty)

    def test_leaving_catalog_or_hovering_a_category_hides_preview(self):
        library = self.window.library
        item = library.findItems("串口", Qt.MatchFlag.MatchExactly | Qt.MatchFlag.MatchRecursive)[0]
        self.hover(item)
        APP.sendEvent(library.viewport(), QEvent(QEvent.Type.Leave))
        self.assertFalse(library.preview.isVisible())
        self.hover(item)
        self.hover(item.parent())
        self.assertFalse(library.preview.isVisible())

    def test_api_can_capture_preview_inside_screen_bounds(self):
        result = self.c.command("library.preview", {"plugin": "io.ssh"})
        self.assertEqual("library.preview", result["target"])
        preview = self.window.library.preview
        self.assertFalse(preview.view.isInteractive())
        self.assertTrue(preview.screen().availableGeometry().contains(preview.geometry()))
        capture = self.c.command("capture", {"target": result["target"]})
        image = QImage(capture["path"])
        self.assertFalse(image.isNull())
        self.assertGreater(image.width(), 200)
        self.assertGreater(image.height(), 100)


if __name__ == "__main__":
    unittest.main()
