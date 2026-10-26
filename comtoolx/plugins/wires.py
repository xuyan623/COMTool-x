"""One-to-one wire rendering and editable paths; payloads remain unchanged."""

from PySide6.QtCore import Qt, QPointF, QTimer
from PySide6.QtGui import QColor, QPen, QPolygonF, QPainterPath
from PySide6.QtWidgets import QGraphicsPathItem, QGraphicsEllipseItem, QGraphicsItem, QMenu

from ..sdk import WireDefinition


def route_paths(source, target, properties, source_rotation=0, target_rotation=0):
    """Build connected segments and retain their geometry for path editing."""
    anchors = [source, *[QPointF(*point) for point in properties.get("points", [])], target]
    directions = (QPointF(1, 0), QPointF(0, 1), QPointF(-1, 0), QPointF(0, -1))
    source_direction = directions[int(source_rotation // 90) % 4]
    target_direction = directions[int(target_rotation // 90) % 4]
    path, segments = QPainterPath(source), []
    for index, (start, end) in enumerate(zip(anchors, anchors[1:])):
        segment = QPainterPath(start)
        if properties.get("style") == "straight":
            segment.lineTo(end)
        else:
            distance = ((end.x() - start.x()) ** 2 + (end.y() - start.y()) ** 2) ** .5
            reach = max(25.0, min(160.0, distance * .45))
            first = (start + source_direction * reach if index == 0 else
                     start + (end - anchors[index - 1]) / 6)
            second = (end - target_direction * reach if index == len(anchors) - 2 else
                      end - (anchors[index + 2] - start) / 6)
            segment.cubicTo(first, second, end)
        path.connectPath(segment)
        segments.append(segment)
    return path, segments


class RouteHandle(QGraphicsEllipseItem):
    """Drag an existing waypoint or insert one by dragging a segment midpoint."""

    def __init__(self, owner, index, position, insertion=False):
        super().__init__(-4, -4, 8, 8, owner)
        self.owner, self.index, self.insertion = owner, index, insertion
        self.changed = False
        self.setPos(position)
        self.setFlags(QGraphicsItem.GraphicsItemFlag.ItemIsMovable |
                      QGraphicsItem.GraphicsItemFlag.ItemIgnoresTransformations)
        self.setBrush(QColor("#222D3B" if insertion else "#9BCBF3"))
        self.setPen(QPen(QColor("#9BCBF3"), 1.5))
        self.setZValue(2)
        self.setCursor(Qt.CursorShape.SizeAllCursor)
        self.setToolTip("拖动新增路径点" if insertion else "拖动调整路径；右键删除此点")

    def mouseMoveEvent(self, event):
        super().mouseMoveEvent(event)
        if self.insertion and not self.changed:
            self.owner.points.insert(self.index, self.pos())
        else:
            self.owner.points[self.index] = self.pos()
        self.changed = True
        # Keep the grabbed handle alive until release; rebuild handles afterwards.
        self.owner.update_endpoints(self.owner.source, self.owner.target, rebuild_handles=False)

    def mouseReleaseEvent(self, event):
        super().mouseReleaseEvent(event)
        if self.changed:
            self.owner.commit_points()

    def contextMenuEvent(self, event):
        if self.insertion:
            event.ignore()
            return
        menu = QMenu()
        remove = menu.addAction("删除路径点")
        if menu.exec(event.screenPos()) is remove:
            self.owner.points.pop(self.index)
            self.owner.commit_points()
        event.accept()


class DirectWire(QGraphicsPathItem):
    """Render above bodies while preserving port gestures and model transactions."""

    def __init__(self, wire, source, target):
        super().__init__()
        self.wire = wire
        self.points = [QPointF(*point) for point in wire.get("properties", {}).get("points", [])]
        self.handles = []
        self.segments = []
        self.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIsSelectable)
        self.setAcceptHoverEvents(True)
        self.setZValue(1)
        self.setToolTip("选中后拖动圆点调整路径；双击导线加点；右键切换线型或重置")
        self.update_endpoints(source, target)

    def update_endpoints(self, source, target, rebuild_handles=True):
        """Recompute path after anchor movement, rotation or live waypoint dragging."""
        self.source, self.target = source, target
        props = {**self.wire.get("properties", {}),
                 "points": [[point.x(), point.y()] for point in self.points]}
        path, self.segments = route_paths(source, target, props,
            self.wire.get("source_rotation", 0), self.wire.get("target_rotation", 0))
        self.setPath(path)
        pen = QPen(QColor(props.get("color", "#6CAEE8")), float(props.get("width", 2)))
        pen.setCapStyle(Qt.PenCapStyle.RoundCap)
        self.setPen(pen)
        if rebuild_handles:
            self.rebuild_handles()

    def rebuild_handles(self):
        """Show edit handles only for a selected wire in a stopped editable graph."""
        for handle in self.handles:
            handle.setParentItem(None)
            if handle.scene():
                handle.scene().removeItem(handle)
        self.handles = []
        if not self.isSelected() or not self.scene() or not self.scene().editor.can_edit():
            return
        for index, point in enumerate(self.points):
            self.handles.append(RouteHandle(self, index, point))
        for index, segment in enumerate(self.segments):
            self.handles.append(RouteHandle(self, index, segment.pointAtPercent(.5), insertion=True))

    def itemChange(self, change, value):
        if change == QGraphicsItem.GraphicsItemChange.ItemSelectedHasChanged and hasattr(self, "handles"):
            self.rebuild_handles()
        return super().itemChange(change, value)

    def commit_points(self):
        """Commit one editable route as one undoable graph command."""
        editor = self.scene().editor
        if not editor.can_edit():
            return
        args = {"id": self.wire["id"], "properties": {
            "points": [[round(point.x(), 2), round(point.y(), 2)] for point in self.points]}}
        QTimer.singleShot(0, lambda: editor.request("graph.wire.patch", args))

    def nearest_segment(self, position):
        """Find the segment under a double-click for ordered waypoint insertion."""
        return min(range(len(self.segments)), key=lambda index: min(
            (position - self.segments[index].pointAtPercent(step / 24)).manhattanLength()
            for step in range(25)))

    def mousePressEvent(self, event):
        # Wires paint over modules, but endpoint presses must reach the actual port.
        if any((event.pos() - point).manhattanLength() <= 12 for point in (self.source, self.target)):
            event.ignore()
            return
        super().mousePressEvent(event)

    def mouseDoubleClickEvent(self, event):
        if self.scene().editor.can_edit():
            self.points.insert(self.nearest_segment(event.pos()), event.pos())
            self.commit_points()
            event.accept()
        else:
            event.ignore()

    def contextMenuEvent(self, event):
        editor = self.scene().editor
        if not editor.can_edit():
            event.ignore()
            return
        menu = QMenu()
        add = menu.addAction("在此添加路径点")
        reset = menu.addAction("重置为自动路径")
        menu.addSeparator()
        curve = menu.addAction("平滑曲线")
        straight = menu.addAction("折线")
        chosen = menu.exec(event.screenPos())
        if chosen is add:
            self.points.insert(self.nearest_segment(event.pos()), event.pos())
            self.commit_points()
        elif chosen is reset:
            self.points = []
            self.commit_points()
        elif chosen in (curve, straight):
            args = {"id": self.wire["id"], "properties": {
                "style": "curve" if chosen is curve else "straight"}}
            QTimer.singleShot(0, lambda: editor.request("graph.wire.patch", args))
        event.accept()

    def paint(self, painter, option, widget=None):
        # Selection is shown by the path and handles, without Qt's large
        # rectangular selection frame crossing otherwise unrelated modules.
        pen = QPen(self.pen())
        if self.isSelected():
            pen.setColor(QColor("#9BCBF3"))
            pen.setWidthF(pen.widthF() + .5)
        painter.setPen(pen)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawPath(self.path())
        path = self.path()
        target = path.pointAtPercent(1)
        direction = target - path.pointAtPercent(.99)
        length = (direction.x() ** 2 + direction.y() ** 2) ** .5
        if length < .001:
            return
        direction /= length
        target -= direction * 6
        normal = QPointF(-direction.y(), direction.x())
        painter.setBrush(self.pen().color())
        painter.setPen(Qt.PenStyle.NoPen)
        painter.drawPolygon(QPolygonF([target, target - direction * 9 - normal * 4,
                                       target - direction * 9 + normal * 4]))


def register(registry):
    registry.register(WireDefinition(
        "wire.direct", "基础导线",
        {"color": {"type": "string", "default": "#6CAEE8", "pattern": "#[0-9a-fA-F]{6}"},
         "width": {"type": "number", "default": 2.0, "min": 1, "max": 8},
         "style": {"type": "string", "default": "curve", "choices": ["curve", "straight"]},
         "points": {"type": "json", "default": [], "format": "points", "label": "路径点",
                    "description": "按走线顺序保存画布坐标，例如 [[300,200],[500,200]]"}},
        DirectWire))
