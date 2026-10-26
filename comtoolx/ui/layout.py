"""Adaptive UI builder and drag/drop layout tree, both backed by project IDs."""

from __future__ import annotations

from PySide6.QtCore import Qt, QTimer, Signal, QMimeData
from PySide6.QtGui import QDrag
from PySide6.QtWidgets import (QWidget, QFrame, QVBoxLayout, QLabel, QScrollArea,
                              QTreeWidget, QTreeWidgetItem, QAbstractItemView)

from ..model import node_by_id

MIME = "application/x-comtoolx-ui-item"


class ViewFrame(QFrame):
    """Designer selection/dragging wraps a view without taking over its runtime."""

    def __init__(self, builder, item):
        super().__init__()
        self.builder, self.item = builder, item
        self.setObjectName(f"{builder.namespace}/{item['id']}")
        self.setProperty("role", "container" if item.get("plugin") else "panel")
        self.setAcceptDrops(bool(item.get("plugin")) and builder.design and not builder.controller.design_locked)
        self.origin = None

    def mousePressEvent(self, event):
        if self.builder.design and not self.builder.controller.design_locked:
            self.builder.selected.emit(self.item["id"])
            self.origin = event.position()
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event):
        if self.builder.design and not self.builder.controller.design_locked and self.origin is not None and event.buttons() & Qt.MouseButton.LeftButton:
            if (event.position() - self.origin).manhattanLength() > 10:
                drag = QDrag(self)
                data = QMimeData()
                data.setData(MIME, self.item["id"].encode())
                drag.setMimeData(data)
                drag.exec(Qt.DropAction.MoveAction)
                self.origin = None
        super().mouseMoveEvent(event)

    def dragEnterEvent(self, event):
        if event.mimeData().hasFormat(MIME):
            event.acceptProposedAction()

    def dropEvent(self, event):
        ident = bytes(event.mimeData().data(MIME)).decode()
        parent = self.item["id"]
        QTimer.singleShot(0, lambda: self.builder.request("ui.item.move", {"id": ident, "parent": parent}))
        event.acceptProposedAction()


class LayoutBuilder(QWidget):
    """Render real plugin views. Design/runtime share the same node instances."""

    selected = Signal(str)

    def __init__(self, controller, request, design=False):
        super().__init__()
        self.controller, self.request, self.design = controller, request, design
        self.namespace = "layout" if design else "runtime"
        self.frames = {}
        self.views = {}
        self.outer = QVBoxLayout(self)
        self.outer.setContentsMargins(0, 0, 0, 0)
        self.root_widget = None

    def synchronize(self):
        if self.root_widget is not None:
            self.outer.removeWidget(self.root_widget)
            self.root_widget.hide()
            self.root_widget.deleteLater()
        self.frames, self.views = {}, {}
        items = self.controller.project["ui"]["items"]
        indexed = {item["id"]: item for item in items}
        root = indexed.get(self.controller.project["ui"]["root"])
        if root is None:
            self.root_widget = QLabel("UI 根容器不存在；请修复工程")
        else:
            self.root_widget = self._build(root, indexed, set())
        self.outer.addWidget(self.root_widget, 1)

    def _build(self, item, indexed, ancestors):
        if item["id"] in ancestors:
            return QLabel("容器循环引用")
        frame = ViewFrame(self, item)
        self.frames[item["id"]] = frame
        layout = QVBoxLayout(frame)
        layout.setContentsMargins(10, 8, 10, 10)
        layout.setSpacing(6)
        title = item.get("title")
        parent = indexed.get(item.get("parent"), {})
        if title and parent.get("plugin") != "layout.tabs":
            label = QLabel(title)
            label.setProperty("role", "panelTitle")
            layout.addWidget(label)
        if item.get("plugin"):
            definition = self.controller.registry.containers.get(item["plugin"])
            children = [(child, self._build(child, indexed, ancestors | {item["id"]}))
                        for child in indexed.values() if child.get("parent") == item["id"]]
            if definition is None:
                content = QLabel(f"缺少布局插件：{item['plugin']}")
            else:
                content = definition.factory(item, children)
            if not children and self.design:
                hint = QLabel("拖入已有视图或布局容器")
                hint.setProperty("role", "muted")
                layout.addWidget(hint)
        else:
            node = next((node for node in self.controller.project["nodes"]
                         if node["id"] == item.get("node")), None)
            definition = self.controller.registry.nodes.get(node["plugin"]) if node else None
            runtime = self.controller.engine.nodes.get(item.get("node"))
            if definition is None or runtime is None or item.get("view") not in definition.views:
                detail = self.controller.engine.initialization_errors.get(item.get("node"), "绑定节点或视图已失效")
                content = QLabel(f"无法呈现：{detail}\n请更新 UI 或修复绑定。")
                content.setWordWrap(True)
            else:
                def action(name, args, nid=node["id"]):
                    return self.request("node.action", {"id": nid, "action": name, "args": args})
                try:
                    content = definition.views[item["view"]](
                        runtime, action, {**item.get("properties", {}), "theme": self.controller.theme,
                                          "design": self.design})
                except Exception as exc:
                    content = QLabel(f"插件视图创建失败：{exc}")
                    content.setWordWrap(True)
                    self.controller.engine.record("error", str(exc))
                self.views[item["id"]] = content
                content.setObjectName(f"{self.namespace}/{item['id']}/view")
                for index, child in enumerate(content.findChildren(QWidget)):
                    name = child.objectName() or f"{type(child).__name__}.{index}"
                    child.setObjectName(f"{self.namespace}/{item['id']}/{name}")
                if self.design:
                    # Keep the actual visual appearance, while clicks select the design.
                    content.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)
        layout.addWidget(content, 1)
        props = item.get("properties", {})
        for key, method in (("min_width", frame.setMinimumWidth), ("min_height", frame.setMinimumHeight),
                            ("max_width", frame.setMaximumWidth), ("max_height", frame.setMaximumHeight)):
            if key in props:
                method(max(0, int(props[key])))
        if "font_size" in props or "font_family" in props:
            font = frame.font()
            if "font_size" in props:
                font.setPointSize(props["font_size"])
            if "font_family" in props:
                font.setFamily(props["font_family"])
            frame.setFont(font)
        styles = []
        if "color" in props:
            styles.append(f"color: {props['color']};")
        if "background" in props:
            styles.append(f"background-color: {props['background']};")
        if styles:
            frame.setProperty("customStyle", True)
            frame.setStyleSheet(f'QFrame[customStyle="true"] {{ {" ".join(styles)} }}')
        return frame

    def select(self, ident):
        for key, frame in self.frames.items():
            frame.setProperty("selected", key == ident)
            frame.style().unpolish(frame)
            frame.style().polish(frame)


class LayoutTree(QTreeWidget):
    """Reorder/reparent existing presentations; adding functionality remains in graph."""

    selected_id = Signal(str)

    def __init__(self, controller, request):
        super().__init__()
        self.controller, self.request = controller, request
        self.setHeaderLabels(["界面结构"])
        self.setObjectName("editor.layout.tree")
        self.setDragDropMode(QAbstractItemView.DragDropMode.InternalMove)
        self.setDefaultDropAction(Qt.DropAction.MoveAction)
        self.itemSelectionChanged.connect(self._selected)

    def synchronize(self):
        self.blockSignals(True)
        self.clear()
        model = self.controller.project["ui"]
        by_id = {}
        remaining = list(model["items"])
        while remaining:
            progress = False
            for item in list(remaining):
                parent = by_id.get(item.get("parent"))
                if item.get("parent") is not None and parent is None:
                    continue
                title = item.get("title") or item.get("view") or item.get("plugin") or item["id"]
                entry = QTreeWidgetItem([title])
                entry.setData(0, Qt.ItemDataRole.UserRole, item["id"])
                entry.setToolTip(0, item["id"])
                if not item.get("plugin"):
                    entry.setFlags(entry.flags() & ~Qt.ItemFlag.ItemIsDropEnabled)
                if parent:
                    parent.addChild(entry)
                else:
                    self.addTopLevelItem(entry)
                by_id[item["id"]] = entry
                remaining.remove(item)
                progress = True
            if not progress:
                break
        self.expandAll()
        self.blockSignals(False)
        self.setDragEnabled(not self.controller.design_locked)
        self.setAcceptDrops(not self.controller.design_locked)

    def _selected(self):
        entries = self.selectedItems()
        if entries:
            self.selected_id.emit(entries[0].data(0, Qt.ItemDataRole.UserRole))

    def dropEvent(self, event):
        if self.controller.design_locked:
            event.ignore()
            return
        selected = self.selectedItems()
        if not selected:
            return
        source_id = selected[0].data(0, Qt.ItemDataRole.UserRole)
        target = self.itemAt(event.position().toPoint())
        if target is None:
            event.ignore()
            return
        model = self.controller.project["ui"]["items"]
        target_id = target.data(0, Qt.ItemDataRole.UserRole)
        target_model = next(item for item in model if item["id"] == target_id)
        if target_model.get("plugin"):
            parent, index = target_id, len([item for item in model if item.get("parent") == target_id])
        else:
            parent = target_model["parent"]
            siblings = [item for item in model if item.get("parent") == parent]
            index = siblings.index(target_model)
        QTimer.singleShot(0, lambda: self.request("ui.item.move", {
            "id": source_id, "parent": parent, "index": index}))
        event.acceptProposedAction()
