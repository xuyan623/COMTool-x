"""Graph editor with draggable typed ports and plugin-provided wire renderers."""

from __future__ import annotations

from PySide6.QtCore import Qt, QPointF, QRectF, QTimer, Signal
from PySide6.QtGui import QColor, QPen, QBrush, QPainter, QPainterPath, QFont, QFontMetricsF
from PySide6.QtWidgets import (QGraphicsView, QGraphicsScene, QGraphicsRectItem,
                              QGraphicsEllipseItem, QGraphicsTextItem, QGraphicsItem,
                              QGraphicsPathItem, QMenu)

TYPE_COLORS = {"bytes": "#6CAEE8", "samples": "#72CBA8", "number": "#E4BD73",
               "bool": "#D6A3E2", "event": "#EEA37D", "text": "#A9BCE6",
               "object": "#7DD4D1", "any": "#B5C1D0"}


class PortItem(QGraphicsEllipseItem):
    """Drag from a port to another; the model validates direction and data types."""

    def __init__(self, node_item, port, index, count):
        super().__init__(-5, -5, 10, 10, node_item)
        self.node_item, self.port = node_item, port
        y = (14 + (index + .5) * (node_item.rect().height() - 28) / count
             if node_item.vertical else node_item.first_port_y + index * 28)
        self.setPos(0 if port.direction == "in" else node_item.width, y)
        self.setBrush(QBrush(QColor(TYPE_COLORS.get(port.data_type, "#B5C1D0"))))
        self.setPen(QPen(QColor("#14202E"), 2))
        self.setAcceptHoverEvents(True)
        self.setToolTip(f"{port.id} · {port.data_type} · {port.direction}")
        self.setCursor(Qt.CursorShape.CrossCursor)

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton and self.scene().editor.can_edit():
            self.scene().editor.begin_wire(self)
            event.accept()
        else:
            event.ignore()

    def mouseMoveEvent(self, event):
        self.scene().editor.move_wire(event.scenePos())

    def mouseReleaseEvent(self, event):
        self.scene().editor.finish_wire(event.scenePos())


class NodeItem(QGraphicsRectItem):
    """A node symbol is a view of a stable model instance, never a runtime owner."""

    def __init__(self, editor, node):
        self.editor, self.node = editor, node
        definition = editor.controller.registry.nodes.get(node["plugin"])
        inputs = [p for p in definition.ports if p.direction == "in"] if definition else []
        outputs = [p for p in definition.ports if p.direction == "out"] if definition else []
        self.width = 230
        super().__init__(0, 0, self.width, 120)
        self.setFlags(QGraphicsItem.GraphicsItemFlag.ItemIsSelectable |
                      QGraphicsItem.GraphicsItemFlag.ItemSendsGeometryChanges |
                      QGraphicsItem.GraphicsItemFlag.ItemIsMovable)
        self.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIsMovable, editor.can_edit())
        self.setPen(QPen(QColor("#3B4B5F"), 1))
        self.setBrush(QBrush(QColor("#222D3B")))
        self.setData(0, node["id"])
        self.overlap_nodes = []
        rotation = node.get("rotation", 0)
        self.vertical = rotation in (90, 270)
        title_font = QFont("Microsoft YaHei UI", 10, QFont.Weight.DemiBold)
        subtitle_font = QFont("Segoe UI", 8)
        # Measure the canonical horizontal symbol before selecting its orientation.
        header_height = QFontMetricsF(title_font).height() + 6 + QFontMetricsF(subtitle_font).height()
        self.header_bottom = 12 + header_height + 12
        self.first_port_y = self.header_bottom + 22
        rows = max(1, len(inputs), len(outputs))
        self.natural_height = self.first_port_y + (rows - 1) * 28 + 22
        height = max(160, rows * 100 + 28) if self.vertical else self.natural_height
        self.setRect(0, 0, self.width, height)
        # A wider vertical symbol keeps horizontal labels apart. Retain the
        # canonical centre so changing orientation does not move the module.
        self.position_offset = QPointF(0, (self.natural_height - height) / 2)
        self.setTransformOriginPoint(self.rect().center())
        self.setPos(QPointF(*node["position"]) + self.position_offset)
        self.setRotation(rotation)
        display_bounds = self.mapRectToScene(self.rect())
        display_width, display_height = display_bounds.width(), display_bounds.height()
        self.text_layer = QGraphicsRectItem(self)
        self.text_layer.setPen(QPen(Qt.PenStyle.NoPen))
        self.text_layer.setBrush(QBrush(Qt.BrushStyle.NoBrush))
        self.text_layer.setAcceptedMouseButtons(Qt.MouseButton.NoButton)
        self.text_layer.setPos(self.mapFromScene(display_bounds.topLeft()))
        self.text_layer.setRotation(-rotation)
        self.text_layer.setRect(0, 0, display_width, display_height)
        # Keep wrappers alive: QGraphicsItem parenting alone does not retain these
        # Python-created QGraphicsTextItem wrappers in the current Qt binding.
        self.text_items = []
        # All text lives in one counter-rotated layer and remains horizontal.
        self.header_group = QGraphicsRectItem(self.text_layer)
        self.header_group.setPen(QPen(Qt.PenStyle.NoPen))
        self.header_group.setBrush(QBrush(Qt.BrushStyle.NoBrush))
        self.header_group.setAcceptedMouseButtons(Qt.MouseButton.NoButton)
        title = QGraphicsTextItem(self.header_group)
        self.text_items.append(title)
        title.setAcceptedMouseButtons(Qt.MouseButton.NoButton)
        title.document().setDocumentMargin(0)
        title.setDefaultTextColor(QColor("#E4EDF6"))
        title.setFont(title_font)
        title.setPlainText(QFontMetricsF(title.font()).elidedText(
            node["name"], Qt.TextElideMode.ElideRight, display_width - 28))
        title.setToolTip(node["name"])
        title.setPos(0, 0)
        subtitle = QGraphicsTextItem(self.header_group)
        self.text_items.append(subtitle)
        subtitle.setAcceptedMouseButtons(Qt.MouseButton.NoButton)
        subtitle.document().setDocumentMargin(0)
        subtitle.setDefaultTextColor(QColor("#92A4BA"))
        subtitle.setFont(subtitle_font)
        subtitle.setPlainText(QFontMetricsF(subtitle.font()).elidedText(
            node["plugin"], Qt.TextElideMode.ElideRight, display_width - 28))
        subtitle.setToolTip(node["plugin"])
        subtitle.setPos(0, title.boundingRect().height() + 6)
        header_height = subtitle.pos().y() + subtitle.boundingRect().height()
        self.header_group.setRect(0, 0, display_width - 28, header_height)
        header_y = ((display_height - header_height) / 2 if self.vertical else
                    12 if rotation == 0 else display_height - header_height - 12)
        self.header_group.setPos(14, header_y)
        separators = ([header_y - 12, header_y + header_height + 12] if self.vertical else
                      [header_y + header_height + 12] if rotation == 0 else [header_y - 12])
        self.separator_lines = [
            (self.text_layer.mapToItem(self, QPointF(0, y)),
             self.text_layer.mapToItem(self, QPointF(display_width, y))) for y in separators]
        self.ports = {}
        for ports in (inputs, outputs):
            for index, port in enumerate(ports):
                item = PortItem(self, port, index, len(ports))
                self.ports[port.id] = item
                label = QGraphicsTextItem(self.text_layer)
                self.text_items.append(label)
                label.setAcceptedMouseButtons(Qt.MouseButton.NoButton)
                label.document().setDocumentMargin(0)
                label.setDefaultTextColor(QColor("#BDD0E4"))
                label.setFont(QFont("Microsoft YaHei UI", 8))
                available = ((display_width - 28) / len(ports) - 12 if self.vertical else
                             display_width / 2 - 28)
                label.setPlainText(QFontMetricsF(label.font()).elidedText(
                    port.label, Qt.TextElideMode.ElideRight, available))
                label.setToolTip(f"{port.label} · {port.id} · {port.data_type}")
                bounds = label.boundingRect()
                centre = self.text_layer.mapFromItem(item, QPointF())
                if self.vertical:
                    label.setPos(centre.x() - bounds.width() / 2,
                                 14 if centre.y() < display_height / 2 else
                                 display_height - 14 - bounds.height())
                else:
                    label.setPos(14 if centre.x() < display_width / 2 else
                                 display_width - 14 - bounds.width(),
                                 centre.y() - bounds.height() / 2)
        self.details = f"{node['id']}\n{definition.description if definition else '缺少插件'}"
        self.setToolTip(self.details)

    def itemChange(self, change, value):
        if change in (QGraphicsItem.GraphicsItemChange.ItemPositionHasChanged,
                      QGraphicsItem.GraphicsItemChange.ItemRotationHasChanged):
            self.editor.update_wires()
        return super().itemChange(change, value)

    def mouseReleaseEvent(self, event):
        super().mouseReleaseEvent(event)
        canonical = self.pos() - self.position_offset
        position = [round(canonical.x(), 1), round(canonical.y(), 1)]
        if position != self.node["position"] and self.editor.can_edit():
            ident = self.node["id"]
            QTimer.singleShot(0, lambda: self.editor.request("graph.node.move", {
                "id": ident, "position": position}))

    def paint(self, painter, option, widget=None):
        super().paint(painter, option, widget)
        if self.isSelected() or self.overlap_nodes:
            painter.setPen(QPen(QColor("#E4BD73" if self.overlap_nodes else "#78B9F2"), 2))
            painter.setBrush(Qt.BrushStyle.NoBrush)
            painter.drawRect(self.rect())
        painter.setPen(QPen(QColor("#35465B"), 1))
        for start, end in self.separator_lines:
            painter.drawLine(start, end)


class GraphEditor(QGraphicsView):
    """Pan/zoom the design without desktop automation; API uses the same commands."""

    selected = Signal(str, str)

    def __init__(self, controller, request):
        self.controller, self.request = controller, request
        self.nodes = {}
        self.wires = {}
        self.overlaps = []
        self.pending_port = None
        self.preview = None
        self.wire_type = "wire.direct"
        scene = QGraphicsScene()
        super().__init__(scene)
        scene.setParent(self)
        scene.editor = self
        scene.selectionChanged.connect(self._selection_changed)
        self.setObjectName("editor.graph")
        self.setRenderHint(QPainter.RenderHint.Antialiasing)
        self.setViewportUpdateMode(QGraphicsView.ViewportUpdateMode.MinimalViewportUpdate)
        self.setDragMode(QGraphicsView.DragMode.RubberBandDrag)
        self.setTransformationAnchor(QGraphicsView.ViewportAnchor.AnchorUnderMouse)
        self.setBackgroundBrush(QColor("#161E29"))
        self.setSceneRect(-2000, -2000, 6000, 5000)
        self.setAcceptDrops(True)

    def dragEnterEvent(self, event):
        if event.mimeData().hasFormat("application/x-comtoolx-plugin") and self.can_edit():
            event.acceptProposedAction()
        else:
            super().dragEnterEvent(event)

    def dragMoveEvent(self, event):
        if event.mimeData().hasFormat("application/x-comtoolx-plugin"):
            event.acceptProposedAction()

    def dropEvent(self, event):
        if event.mimeData().hasFormat("application/x-comtoolx-plugin") and self.can_edit():
            plugin = bytes(event.mimeData().data("application/x-comtoolx-plugin")).decode()
            position = self.mapToScene(event.position().toPoint())
            self.request("graph.node.add", {"plugin": plugin, "position": [position.x(), position.y()]})
            event.acceptProposedAction()
        else:
            super().dropEvent(event)

    def can_edit(self):
        return not self.controller.design_locked and not self.controller.engine.running

    def synchronize(self):
        """Rebuild symbols from model data while preserving the current camera."""
        center = self.mapToScene(self.viewport().rect().center())
        selection = {item.data(0) for item in self.scene().selectedItems()}
        self.pending_port = self.preview = None
        self.scene().blockSignals(True)
        self.scene().clear()
        self.nodes, self.wires = {}, {}
        for node in self.controller.project["nodes"]:
            item = NodeItem(self, node)
            self.nodes[node["id"]] = item
            self.scene().addItem(item)
        for wire in self.controller.project["wires"]:
            definition = self.controller.registry.wires.get(wire["plugin"])
            endpoints = self.endpoints(wire)
            if definition and endpoints:
                # Render-only orientation augments the wire contract without
                # duplicating node orientation in saved connection data.
                geometry = {**wire,
                    "source_rotation": self.nodes[wire["source"]["node"]].rotation(),
                    "target_rotation": self.nodes[wire["target"]["node"]].rotation()}
                item = definition.factory(geometry, *endpoints)
                item.setData(0, wire["id"])
                self.wires[wire["id"]] = item
                self.scene().addItem(item)
        for item in [*self.nodes.values(), *self.wires.values()]:
            if item.data(0) in selection:
                item.setSelected(True)
        self.scene().blockSignals(False)
        self.check_occupancy()
        self.centerOn(center)

    def check_occupancy(self):
        """Report overlapping rotated bodies without changing the user's placement."""
        self.overlaps = []
        items = list(self.nodes.values())
        for item in items:
            item.overlap_nodes = []
        for index, first in enumerate(items):
            first_bounds = first.mapRectToScene(first.rect())
            for second in items[index + 1:]:
                intersection = first_bounds.intersected(second.mapRectToScene(second.rect()))
                if intersection.isEmpty():
                    continue
                first.overlap_nodes.append(second.node["name"])
                second.overlap_nodes.append(first.node["name"])
                self.overlaps.append({
                    "nodes": [first.node["id"], second.node["id"]],
                    "bounds": {"x": intersection.x(), "y": intersection.y(),
                               "width": intersection.width(), "height": intersection.height()}})
        for item in items:
            suffix = ("\n与这些模块重叠：" + "、".join(item.overlap_nodes)
                      + "\n可移动模块调整位置。" if item.overlap_nodes else "")
            item.setToolTip(item.details + suffix)
            item.update()

    def endpoints(self, wire):
        points = []
        for side in ("source", "target"):
            endpoint = wire[side]
            node = self.nodes.get(endpoint["node"])
            port = node.ports.get(endpoint["port"]) if node else None
            if port is None:
                return None
            points.append(port.scenePos())
        return points

    def update_wires(self):
        for wire in self.controller.project["wires"]:
            item = self.wires.get(wire["id"])
            endpoints = self.endpoints(wire)
            if item and endpoints:
                item.update_endpoints(*endpoints)

    def begin_wire(self, port):
        self.pending_port = port
        self.preview = QGraphicsPathItem()
        self.preview.setPen(QPen(QColor("#9BCBF3"), 2, Qt.PenStyle.DashLine))
        self.preview.setZValue(2)
        self.scene().addItem(self.preview)
        self.move_wire(port.scenePos())

    def move_wire(self, position):
        if self.preview is not None:
            path = QPainterPath(self.pending_port.scenePos())
            path.lineTo(position)
            self.preview.setPath(path)

    def finish_wire(self, position):
        if self.pending_port is None:
            return
        source = self.pending_port
        candidates = self.scene().items(position)
        target = next((item for item in candidates if isinstance(item, PortItem) and item is not source), None)
        self.scene().removeItem(self.preview)
        self.preview = self.pending_port = None
        if target is None:
            return
        if source.port.direction == "in":
            source, target = target, source
        args = {"plugin": self.wire_type,
                "source": {"node": source.node_item.node["id"], "port": source.port.id},
                "target": {"node": target.node_item.node["id"], "port": target.port.id}}
        QTimer.singleShot(0, lambda: self.request("graph.wire.add", args))

    def _selection_changed(self):
        items = self.scene().selectedItems()
        if items:
            item = items[0]
            self.selected.emit("node" if isinstance(item, NodeItem) else "wire", str(item.data(0)))

    def fit_content(self):
        bounds = self.scene().itemsBoundingRect()
        if not bounds.isEmpty():
            self.fitInView(bounds.adjusted(-60, -60, 60, 60), Qt.AspectRatioMode.KeepAspectRatio)

    def wheelEvent(self, event):
        factor = 1.15 if event.angleDelta().y() > 0 else 1 / 1.15
        scale = self.transform().m11() * factor
        if 0.15 <= scale <= 3:
            self.scale(factor, factor)

    def keyPressEvent(self, event):
        if event.key() == Qt.Key.Key_Space:
            if self.can_edit() and not event.isAutoRepeat():
                commands = [{"op": "graph.node.patch", "args": {
                    "id": item.node["id"], "rotation": (item.node.get("rotation", 0) + 90) % 360}}
                    for item in self.scene().selectedItems() if isinstance(item, NodeItem)]
                if commands:
                    self.request("batch", {"commands": commands})
            event.accept()
            return
        if event.key() in (Qt.Key.Key_Delete, Qt.Key.Key_Backspace) and self.can_edit():
            operations = []
            for item in self.scene().selectedItems():
                operations.append({"op": "graph.node.remove" if isinstance(item, NodeItem)
                                   else "graph.wire.remove", "args": {"id": item.data(0)}})
            if operations:
                self.request("batch", {"commands": operations})
            return
        if event.key() == Qt.Key.Key_F:
            self.fit_content()
            return
        super().keyPressEvent(event)

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.MiddleButton:
            self.setDragMode(QGraphicsView.DragMode.ScrollHandDrag)
            self._pan_position = event.position()
            event.accept()
        else:
            super().mousePressEvent(event)

    def mouseMoveEvent(self, event):
        if event.buttons() & Qt.MouseButton.MiddleButton and hasattr(self, "_pan_position"):
            delta = event.position() - self._pan_position
            self._pan_position = event.position()
            self.horizontalScrollBar().setValue(self.horizontalScrollBar().value() - int(delta.x()))
            self.verticalScrollBar().setValue(self.verticalScrollBar().value() - int(delta.y()))
        else:
            super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.MouseButton.MiddleButton:
            self.setDragMode(QGraphicsView.DragMode.RubberBandDrag)
            event.accept()
        else:
            super().mouseReleaseEvent(event)

    def drawBackground(self, painter, rect):
        super().drawBackground(painter, rect)
        step = 24
        left = int(rect.left()) - int(rect.left()) % step
        top = int(rect.top()) - int(rect.top()) % step
        painter.setPen(QPen(QColor("#2B3544"), 1))
        points = [QPointF(x, y) for x in range(left, int(rect.right()), step)
                  for y in range(top, int(rect.bottom()), step)]
        if points:
            painter.drawPoints(points)
