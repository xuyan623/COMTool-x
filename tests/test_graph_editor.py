"""Native graph interactions: geometry, layering, port gestures and saved rotation."""

import tempfile
import unittest
from pathlib import Path

from PySide6.QtCore import Qt, QPointF, QPoint, QTimer
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication

from comtoolx.controller import Controller
from comtoolx.sdk import Registry, WorkbenchError
from comtoolx.plugins import register_builtins
from comtoolx.ui.graph import GraphEditor

APP = QApplication.instance() or QApplication([])
ROOT = Path(__file__).resolve().parent.parent


class GraphEditorTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=ROOT / ".sessions")
        registry = Registry()
        register_builtins(registry)
        self.c = Controller(registry, self.temporary.name)
        self.c.command("graph.node.add", {"id": "serial", "plugin": "io.serial", "position": [0, 0]})
        self.c.command("graph.node.add", {"id": "decoder", "plugin": "protocol.plot", "position": [360, 0]})
        self.c.command("graph.wire.add", {"id": "link",
            "source": {"node": "serial", "port": "rx"},
            "target": {"node": "decoder", "port": "bytes"}})
        self.view = GraphEditor(self.c, self.c.command)
        self.c.changed.connect(lambda kind: self.view.synchronize())
        self.view.resize(1000, 650)
        self.view.synchronize()
        self.view.show()
        self.view.fit_content()
        APP.processEvents()

    def tearDown(self):
        self.view.close()
        self.c.close()
        self.temporary.cleanup()

    def test_output_label_alignment_and_header_spacing(self):
        node = self.view.nodes["serial"]
        title, subtitle, left, right = node.text_items
        self.assertGreaterEqual(subtitle.pos().y() - title.pos().y() - title.boundingRect().height(), 6)
        self.assertGreaterEqual(left.pos().y() - subtitle.pos().y() - subtitle.boundingRect().height(), 20)
        self.assertAlmostEqual(node.width - 14, right.pos().x() + right.boundingRect().width())
        self.assertAlmostEqual(node.ports["rx"].pos().y(), right.pos().y() + right.boundingRect().height() / 2)

    def test_rotated_text_stays_readable_and_inside_its_layout_regions(self):
        from dataclasses import replace
        original = self.c.registry.nodes["io.ssh"]
        plugin_id = "example." + "long_plugin_identifier_" * 12
        self.c.registry.register(replace(original, id=plugin_id,
            ports=tuple(replace(port, label="很长的端口说明_" * 8) for port in original.ports)))
        self.c.command("graph.node.add", {"id": "long", "plugin": plugin_id,
            "name": "中英文Mixed长标题_" * 12, "position": [0, 350]})
        for angle in (0, 90, 180, 270):
            self.c.command("graph.node.patch", {"id": "long", "rotation": angle})
            node = self.view.nodes["long"]
            title, subtitle, *labels = node.text_items
            header = node.header_group.mapRectToItem(node, node.header_group.rect())
            for text in node.text_items:
                bounds = text.mapRectToItem(node, text.boundingRect())
                self.assertTrue(node.rect().adjusted(-.1, -.1, .1, .1).contains(bounds), bounds)
                direction = text.mapToScene(QPointF(1, 0)) - text.mapToScene(QPointF())
                self.assertAlmostEqual(1, direction.x(), places=6)
                self.assertAlmostEqual(0, direction.y(), places=6)
            for text in (title, subtitle):
                self.assertTrue(header.adjusted(-.1, -.1, .1, .1).contains(
                    text.mapRectToItem(node, text.boundingRect())))
            for label in labels:
                self.assertFalse(header.intersects(label.mapRectToItem(node, label.boundingRect())))
            for index, label in enumerate(labels):
                for other in labels[index + 1:]:
                    self.assertFalse(label.mapRectToScene(label.boundingRect()).intersects(
                        other.mapRectToScene(other.boundingRect())))
            first = title.mapRectToScene(title.boundingRect())
            second = subtitle.mapRectToScene(subtitle.boundingRect())
            gap = second.top() - first.bottom()
            self.assertAlmostEqual(6, gap, places=6)
            self.assertEqual(plugin_id, subtitle.toolTip())
            self.assertEqual(10, title.font().pointSize())
            self.assertEqual(8, subtitle.font().pointSize())

    def test_dragging_vertical_module_saves_its_canonical_position(self):
        self.c.command("graph.node.patch", {"id": "serial", "rotation": 90})
        node = self.view.nodes["serial"]
        start = self.view.mapFromScene(node.mapToScene(node.rect().center()))
        end = start + QPoint(50, 30)
        QTest.mousePress(self.view.viewport(), Qt.MouseButton.LeftButton, pos=start)
        QTest.mouseMove(self.view.viewport(), end, delay=20)
        QTest.mouseRelease(self.view.viewport(), Qt.MouseButton.LeftButton, pos=end)
        APP.processEvents()
        node = self.view.nodes["serial"]
        canonical = node.pos() - node.position_offset
        self.assertAlmostEqual(self.c.project["nodes"][0]["position"][0], canonical.x(), places=1)
        self.assertAlmostEqual(self.c.project["nodes"][0]["position"][1], canonical.y(), places=1)
        centre = node.mapToScene(node.rect().center())
        self.c.command("graph.node.patch", {"id": "serial", "rotation": 0})
        self.assertEqual(centre, self.view.nodes["serial"].mapToScene(self.view.nodes["serial"].rect().center()))

    def test_header_text_still_selects_and_rotates_the_module(self):
        title = self.view.nodes["serial"].text_items[0]
        point = self.view.mapFromScene(title.mapToScene(title.boundingRect().center()))
        QTest.mouseClick(self.view.viewport(), Qt.MouseButton.LeftButton, pos=point)
        self.assertTrue(self.view.nodes["serial"].isSelected())
        QTest.keyClick(self.view, Qt.Key.Key_Space)
        self.assertEqual(90, self.view.nodes["serial"].rotation())

    def test_rotated_occupancy_is_reported_and_clears_after_moving(self):
        self.c.command("graph.node.add", {"id": "near", "plugin": "display.value", "position": [80, 150]})
        self.assertEqual([], self.view.overlaps)
        self.c.command("graph.node.patch", {"id": "serial", "rotation": 90})
        self.assertEqual({"serial", "near"}, set(self.view.overlaps[0]["nodes"]))
        self.assertIn("重叠", self.view.nodes["serial"].toolTip())
        self.assertEqual([0, 0], self.c.project["nodes"][0]["position"])
        self.c.command("graph.node.move", {"id": "near", "position": [500, 400]})
        self.assertEqual([], self.view.overlaps)
        self.assertNotIn("重叠", self.view.nodes["serial"].toolTip())

    def test_space_rotation_preserves_selection_wires_and_saved_orientation(self):
        node = self.view.nodes["serial"]
        node.setSelected(True)
        centre = node.mapToScene(node.rect().center())
        for angle in (90, 180, 270, 0, 90):
            QTest.keyClick(self.view, Qt.Key.Key_Space)
            APP.processEvents()
            node = self.view.nodes["serial"]
            self.assertEqual(angle, node.rotation())
            self.assertTrue(node.isSelected())
            self.assertEqual(centre, node.mapToScene(node.rect().center()))
            self.assertEqual(node.ports["rx"].scenePos(),
                             self.view.wires["link"].path().pointAtPercent(0))
        target = Path(self.temporary.name) / "rotated.comtoolx"
        self.c.command("project.save", {"path": str(target)})
        self.c.command("project.open", {"path": str(target)})
        self.assertEqual(90, self.view.nodes["serial"].rotation())
        with self.assertRaises(WorkbenchError):
            self.c.command("history.undo")
        self.assertEqual(90, self.view.nodes["serial"].rotation())

    def test_rotation_undo_and_design_guards(self):
        self.view.nodes["serial"].setSelected(True)
        QTest.keyClick(self.view, Qt.Key.Key_Space)
        self.c.command("history.undo")
        self.assertEqual(0, self.view.nodes["serial"].rotation())
        self.c.command("history.redo")
        self.assertEqual(90, self.view.nodes["serial"].rotation())
        self.c.command("mode.set", {"mode": "runtime"})
        QTest.keyClick(self.view, Qt.Key.Key_Space)
        self.assertEqual(90, self.view.nodes["serial"].rotation())
        self.c.command("mode.set", {"mode": "edit"})
        self.c.command("runtime.start")
        QTest.keyClick(self.view, Qt.Key.Key_Space)
        self.assertEqual(90, self.view.nodes["serial"].rotation())
        self.c.command("runtime.stop")
        with self.assertRaises(WorkbenchError):
            self.c.command("graph.node.patch", {"id": "serial", "rotation": 45})
        self.assertEqual(90, self.view.nodes["serial"].rotation())

    def test_foreground_wire_does_not_steal_port_drag(self):
        self.assertGreater(self.view.wires["link"].zValue(), self.view.nodes["serial"].zValue())
        port = self.view.nodes["serial"].ports["rx"]
        start = self.view.mapFromScene(port.scenePos())
        QTest.mousePress(self.view.viewport(), Qt.MouseButton.LeftButton, pos=start)
        self.assertIs(port, self.view.pending_port)
        end = self.view.mapFromScene(port.scenePos() + QPointF(80, 160))
        QTest.mouseMove(self.view.viewport(), end)
        QTest.mouseRelease(self.view.viewport(), Qt.MouseButton.LeftButton, pos=end)
        self.assertIsNone(self.view.pending_port)

    def test_wire_remains_visible_when_another_node_overlaps_it(self):
        from PySide6.QtCore import QRectF
        from PySide6.QtGui import QImage, QPainter, QColor
        self.c.command("graph.node.add", {"id": "overlap", "plugin": "display.value",
                                         "position": [250, 20]})
        image = QImage(650, 220, QImage.Format.Format_ARGB32)
        image.fill(Qt.GlobalColor.black)
        painter = QPainter(image)
        self.view.scene().render(painter, QRectF(0, 0, 650, 220), QRectF(0, 0, 650, 220))
        painter.end()
        y = round(self.view.nodes["serial"].ports["rx"].scenePos().y())
        self.assertEqual(QColor("#6CAEE8"), image.pixelColor(300, y))

    def test_drag_route_point_and_save_undo_redo(self):
        wire = self.view.wires["link"]
        wire.setSelected(True)
        handle = next(item for item in wire.handles if item.insertion)
        start = self.view.mapFromScene(handle.scenePos())
        QTest.mousePress(self.view.viewport(), Qt.MouseButton.LeftButton, pos=start)
        QTest.mouseMove(self.view.viewport(), start + QPoint(0, 90), delay=20)
        QTest.mouseRelease(self.view.viewport(), Qt.MouseButton.LeftButton, pos=start + QPoint(0, 90))
        APP.processEvents()
        points = self.c.project["wires"][0]["properties"]["points"]
        self.assertEqual(1, len(points))
        self.assertGreater(points[0][1], wire.source.y() + 30)
        self.c.command("history.undo")
        self.assertEqual([], self.c.project["wires"][0]["properties"]["points"])
        self.c.command("history.redo")
        self.assertEqual(points, self.c.project["wires"][0]["properties"]["points"])
        path = Path(self.temporary.name) / "route.comtoolx"
        self.c.command("project.save", {"path": str(path)})
        self.c.command("project.open", {"path": str(path)})
        self.assertEqual(points, self.c.project["wires"][0]["properties"]["points"])

    def test_double_click_adds_a_waypoint(self):
        wire = self.view.wires["link"]
        position = self.view.mapFromScene(wire.path().pointAtPercent(.25))
        QTest.mouseDClick(self.view.viewport(), Qt.MouseButton.LeftButton, pos=position)
        APP.processEvents()
        self.assertEqual(1, len(self.c.project["wires"][0]["properties"]["points"]))

    def test_remove_waypoint_from_its_context_menu(self):
        from PySide6.QtWidgets import QGraphicsSceneContextMenuEvent
        self.c.command("graph.wire.patch", {"id": "link", "properties": {"points": [[290, 180]]}})
        wire = self.view.wires["link"]
        wire.setSelected(True)
        handle = next(item for item in wire.handles if not item.insertion)
        event = QGraphicsSceneContextMenuEvent()
        event.setScreenPos(self.view.viewport().mapToGlobal(self.view.mapFromScene(handle.scenePos())))
        def choose_remove():
            menu = APP.activePopupWidget()
            menu.setActiveAction(menu.actions()[0])
            QTest.keyClick(menu, Qt.Key.Key_Return)
        QTimer.singleShot(10, choose_remove)
        handle.contextMenuEvent(event)
        APP.processEvents()
        self.assertEqual([], self.c.project["wires"][0]["properties"]["points"])

    def test_route_coordinates_are_checked_and_locked_handles_are_hidden(self):
        for points in ([[1]], [[True, 2]], [[1, float("nan")]], "bad"):
            with self.assertRaises(WorkbenchError):
                self.c.command("graph.wire.patch", {"id": "link", "properties": {"points": points}})
        self.view.wires["link"].setSelected(True)
        self.assertTrue(self.view.wires["link"].handles)
        self.c.command("mode.set", {"mode": "runtime"})
        self.assertEqual([], self.view.wires["link"].handles)


if __name__ == "__main__":
    unittest.main()
