"""The plugin-neutral shell: graph, UI design, runtime and internal automation."""

from __future__ import annotations

import json
import uuid
from pathlib import Path

from PySide6.QtCore import Qt, QTimer, QMimeData, QRectF, QEvent, QPoint
from PySide6.QtGui import QAction, QDrag, QImage, QPainter, QColor
from PySide6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QSplitter, QTreeWidget, QTreeWidgetItem, QLineEdit, QPlainTextEdit, QTextEdit,
    QComboBox, QPushButton, QToolButton, QToolBar, QLabel, QStackedWidget, QScrollArea,
    QFileDialog, QMessageBox, QInputDialog, QMenu, QDialog, QDialogButtonBox,
    QAbstractButton, QSpinBox, QDoubleSpinBox, QSlider, QCheckBox, QTabWidget,
    QAbstractItemView, QFrame, QGraphicsScene, QGraphicsView)

from ..controller import COMMANDS
from ..model import ui_by_id
from ..sdk import WorkbenchError, json_value
from .graph import GraphEditor, NodeItem
from .layout import LayoutBuilder, LayoutTree
from .properties import Inspector
from .theme import apply_theme, prepare_fonts


class LibraryPreview(QFrame):
    """A catalogue preview renders the real symbol from metadata, without runtime IO."""

    def __init__(self, library):
        super().__init__(library, Qt.WindowType.ToolTip)
        self.setObjectName("library.preview")
        self.setProperty("role", "panel")
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)
        self.plugin_id = None
        self.node = None
        layout = QVBoxLayout(self)
        layout.setContentsMargins(14, 14, 14, 14)
        layout.setSpacing(8)
        self.scene = QGraphicsScene(self)
        self.view = QGraphicsView(self.scene)
        self.view.setObjectName("library.preview.canvas")
        self.view.setFrameShape(QFrame.Shape.NoFrame)
        self.view.setInteractive(False)
        self.view.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.view.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.view.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.view.setRenderHint(QPainter.RenderHint.Antialiasing)
        self.view.setBackgroundBrush(QColor("#161E29"))
        layout.addWidget(self.view)
        self.description = QLabel()
        self.description.setObjectName("library.preview.description")
        self.description.setTextFormat(Qt.TextFormat.PlainText)
        self.description.setWordWrap(True)
        layout.addWidget(self.description)
        hint = QLabel("拖入画布或双击添加")
        hint.setProperty("role", "muted")
        layout.addWidget(hint)

    def set_definition(self, definition, editor, width):
        """Reuse the graph renderer; plugin factories and device actions are not called."""
        self.scene.clear()
        self.node = NodeItem(editor, {"id": "catalog-preview", "plugin": definition.id,
            "name": definition.name, "position": [0, 0], "rotation": 0})
        self.scene.addItem(self.node)
        self.plugin_id = definition.id
        bounds = self.scene.itemsBoundingRect().adjusted(-12, -12, 12, 12)
        self.view.setSceneRect(bounds)
        self.setFixedWidth(width)
        self.view.setFixedSize(width - 28, min(360, max(140, int(bounds.height()) + 8)))
        self.description.setText(definition.description)
        self.adjustSize()


class Library(QTreeWidget):
    """Plugin catalogue entries can be dragged to the graph or double-clicked."""

    def __init__(self, controller):
        super().__init__()
        self.controller = controller
        self.editor = None
        self.preview = None
        self.setMouseTracking(True)

    def show_preview(self, ident, position):
        """Place a non-activating preview beside the pointer, within the current screen."""
        definition = self.controller.registry.nodes.get(ident)
        if definition is None or self.editor is None:
            raise WorkbenchError("插件不存在或功能图尚未就绪")
        if self.preview is None:
            self.preview = LibraryPreview(self)
        screen = QApplication.screenAt(position) or self.screen()
        available = screen.availableGeometry()
        self.preview.set_definition(definition, self.editor, min(360, available.width() - 16))
        target = position + QPoint(16, 18)
        self.preview.move(
            max(available.left() + 8, min(target.x(), available.right() - self.preview.width() - 8)),
            max(available.top() + 8, min(target.y(), available.bottom() - self.preview.height() - 8)))
        self.preview.show()
        self.preview.view.fitInView(self.preview.view.sceneRect(), Qt.AspectRatioMode.KeepAspectRatio)
        return {"plugin": ident, "target": "library.preview",
                "ports": [vars(port) for port in definition.ports]}

    def viewportEvent(self, event):
        if event.type() == QEvent.Type.ToolTip:
            item = self.itemAt(event.pos())
            ident = item.data(0, Qt.ItemDataRole.UserRole) if item else None
            if ident:
                self.show_preview(ident, event.globalPos())
            elif self.preview:
                self.preview.hide()
            event.accept()
            return True
        if event.type() == QEvent.Type.Leave and self.preview:
            self.preview.hide()
        return super().viewportEvent(event)

    def mouseMoveEvent(self, event):
        if self.preview and self.preview.isVisible():
            item = self.itemAt(event.position().toPoint())
            ident = item.data(0, Qt.ItemDataRole.UserRole) if item else None
            if ident != self.preview.plugin_id:
                self.preview.hide()
        super().mouseMoveEvent(event)

    def startDrag(self, supported_actions):
        if self.preview:
            self.preview.hide()
        selected = self.selectedItems()
        if not selected:
            return
        ident = selected[0].data(0, Qt.ItemDataRole.UserRole)
        if ident:
            drag = QDrag(self)
            data = QMimeData()
            data.setData("application/x-comtoolx-plugin", ident.encode())
            drag.setMimeData(data)
            drag.exec(Qt.DropAction.CopyAction)


class MainWindow(QMainWindow):
    """Keep native UI editing and local API operations on one checked command surface."""

    def __init__(self, controller):
        super().__init__()
        prepare_fonts()
        self.controller = controller
        controller.window = self
        self.current_workspace = "graph"
        self.selected_object = None
        self._last_result = None
        self.api_info = {}
        self.setObjectName("window")
        self.resize(1480, 920)
        self.setMinimumSize(980, 620)
        self._build_menu()
        self._build_toolbar()
        self.pages = QStackedWidget()
        self.pages.setObjectName("workspace")
        self.setCentralWidget(self.pages)
        self._build_graph_page()
        self._build_layout_page()
        self.runtime_preview = LayoutBuilder(controller, self.safe_command, design=False)
        self.runtime_preview.setObjectName("preview.runtime")
        self.runtime_scroll = QScrollArea()
        self.runtime_scroll.setWidgetResizable(True)
        self.runtime_scroll.setWidget(self.runtime_preview)
        self.pages.addWidget(self.runtime_scroll)
        self.status_label = QLabel()
        self.statusBar().addPermanentWidget(self.status_label)
        self.statusBar().showMessage("从插件库拖入节点，或打开已有工程。")
        controller.changed.connect(self.refresh)
        controller.notice.connect(self.show_notice)
        self.poll_timer = QTimer(self)
        self.poll_timer.timeout.connect(self.poll_status)
        self.poll_timer.start(250)
        self.refresh("project")

    def _build_menu(self):
        file_menu = self.menuBar().addMenu("工程")
        actions = [
            ("新建工程", "Ctrl+N", "project.new", lambda: self.new_project()),
            ("关闭工程", "", "project.close", self.new_project),
            ("打开工程…", "Ctrl+O", "project.open", lambda: self.open_project(False)),
            ("只读运行打开…", "Ctrl+Shift+O", "project.open_runtime", lambda: self.open_project(True)),
            ("保存", "Ctrl+S", "project.save", lambda: self.save_project(False)),
            ("另存为…", "Ctrl+Shift+S", "project.save_as", lambda: self.save_project(True)),
            ("重命名工程…", "", "project.rename", lambda: self.rename_project()),
        ]
        for label, shortcut, name, callback in actions:
            action = QAction(label, self)
            action.setObjectName(f"action.{name}")
            if shortcut:
                action.setShortcut(shortcut)
            action.triggered.connect(callback)
            file_menu.addAction(action)
        edit_menu = self.menuBar().addMenu("编辑")
        for label, shortcut, op in (("撤销", "Ctrl+Z", "history.undo"),
                                    ("重做", "Ctrl+Shift+Z", "history.redo")):
            action = QAction(label, self)
            action.setShortcut(shortcut)
            action.setObjectName("action." + op)
            action.triggered.connect(lambda checked=False, command=op: self.safe_command(command, {}))
            edit_menu.addAction(action)
        plugins_menu = self.menuBar().addMenu("插件")
        plugins_menu.addAction("加载本地插件…", self.load_plugin)
        plugins_menu.addAction("查看插件契约", self.show_registry)
        view_menu = self.menuBar().addMenu("视图")
        view_menu.addAction("深色主题", lambda: self.safe_command("theme.set", {"theme": "dark"}))
        view_menu.addAction("浅色主题", lambda: self.safe_command("theme.set", {"theme": "light"}))
        fullscreen = QAction("全屏", self)
        fullscreen.setShortcut("F11")
        fullscreen.triggered.connect(lambda: self.showNormal() if self.isFullScreen() else self.showFullScreen())
        view_menu.addAction(fullscreen)
        help_menu = self.menuBar().addMenu("帮助")
        help_menu.addAction("本地 API 与 Skill", self.show_api)
        help_menu.addAction("检查工程", lambda: self.show_validation())

    def _build_toolbar(self):
        toolbar = QToolBar()
        toolbar.setMovable(False)
        toolbar.setObjectName("toolbar")
        self.addToolBar(toolbar)
        self.project_label = QLabel("COMTool-x")
        self.project_label.setProperty("role", "heading")
        toolbar.addWidget(self.project_label)
        toolbar.addSeparator()
        self.workspace_buttons = {}
        for key, label in (("graph", "功能图"), ("layout", "界面设计"), ("runtime", "运行界面")):
            button = QToolButton()
            button.setText(label)
            button.setCheckable(True)
            button.setObjectName(f"workspace.{key}")
            button.clicked.connect(lambda checked=False, view=key: self.set_workspace(view))
            toolbar.addWidget(button)
            self.workspace_buttons[key] = button
        spacer = QWidget()
        from PySide6.QtWidgets import QSizePolicy
        spacer.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
        toolbar.addWidget(spacer)
        self.update_button = QPushButton("更新 UI")
        self.update_button.setObjectName("command.ui.update")
        self.update_button.clicked.connect(lambda: self.update_ui())
        toolbar.addWidget(self.update_button)
        self.mode_button = QPushButton("进入运行只读")
        self.mode_button.setObjectName("command.mode")
        self.mode_button.clicked.connect(lambda: self.safe_command("mode.set", {
            "mode": "edit" if self.controller.design_locked else "runtime"}))
        toolbar.addWidget(self.mode_button)
        self.start_button = QPushButton("运行")
        self.start_button.setProperty("primary", True)
        self.start_button.setObjectName("command.runtime.start")
        self.start_button.clicked.connect(lambda: self.safe_command("runtime.start", {}))
        self.stop_button = QPushButton("停止")
        self.stop_button.setObjectName("command.runtime.stop")
        self.stop_button.clicked.connect(lambda: self.safe_command("runtime.stop", {}))
        toolbar.addWidget(self.start_button)
        toolbar.addWidget(self.stop_button)

    def _build_graph_page(self):
        splitter = QSplitter()
        library_panel = QWidget()
        layout = QVBoxLayout(library_panel)
        layout.addWidget(QLabel("插件库"))
        search = QLineEdit()
        search.setPlaceholderText("搜索功能、插件 ID")
        search.setObjectName("library.search")
        layout.addWidget(search)
        self.library = Library(self.controller)
        self.library.setHeaderHidden(True)
        self.library.setDragEnabled(True)
        self.library.setObjectName("library.nodes")
        layout.addWidget(self.library, 1)
        self.library.itemDoubleClicked.connect(self.add_library_node)
        search.textChanged.connect(self.filter_library)
        center = QWidget()
        center_layout = QVBoxLayout(center)
        center_layout.setContentsMargins(0, 0, 0, 0)
        tools = QHBoxLayout()
        self.wire_combo = QComboBox()
        self.wire_combo.setObjectName("graph.wire_type")
        tools.addWidget(QLabel("导线"))
        tools.addWidget(self.wire_combo)
        fit = QPushButton("适配视野")
        fit.setObjectName("graph.fit")
        fit.clicked.connect(lambda: self.graph.fit_content())
        tools.addWidget(fit)
        tools.addStretch()
        hint = QLabel("端口连线 · 拖动线点改形 · 空格旋转")
        hint.setToolTip("拖拽端口连线；选中导线后拖动圆点，双击加点，右键编辑；中键平移，滚轮缩放。")
        hint.setProperty("role", "muted")
        tools.addWidget(hint)
        center_layout.addLayout(tools)
        self.graph = GraphEditor(self.controller, self.safe_command)
        self.library.editor = self.graph
        center_layout.addWidget(self.graph, 1)
        self.wire_combo.currentIndexChanged.connect(
            lambda: setattr(self.graph, "wire_type", self.wire_combo.currentData() or "wire.direct"))
        self.graph_inspector = Inspector(self.controller, self.safe_command)
        self.graph.selected.connect(lambda kind, ident: self.select_object(kind, ident))
        splitter.addWidget(library_panel)
        splitter.addWidget(center)
        splitter.addWidget(self.graph_inspector)
        splitter.setStretchFactor(1, 1)
        splitter.setSizes([235, 920, 300])
        self.pages.addWidget(splitter)

    def _build_layout_page(self):
        splitter = QSplitter()
        outline = QWidget()
        layout = QVBoxLayout(outline)
        buttons = QHBoxLayout()
        add_container = QToolButton()
        add_container.setText("添加分区")
        add_container.setPopupMode(QToolButton.ToolButtonPopupMode.InstantPopup)
        self.container_menu = QMenu(add_container)
        add_container.setMenu(self.container_menu)
        buttons.addWidget(add_container)
        remove = QPushButton("移除")
        remove.setObjectName("layout.remove")
        remove.clicked.connect(self.remove_ui_item)
        buttons.addWidget(remove)
        layout.addLayout(buttons)
        self.view_combo = QComboBox()
        self.view_combo.setObjectName("layout.available_views")
        layout.addWidget(self.view_combo)
        add_view = QPushButton("添加已有功能视图")
        add_view.setObjectName("layout.add_view")
        add_view.clicked.connect(self.add_existing_view)
        layout.addWidget(add_view)
        self.layout_tree = LayoutTree(self.controller, self.safe_command)
        layout.addWidget(self.layout_tree, 1)
        self.design_preview = LayoutBuilder(self.controller, self.safe_command, design=True)
        self.design_preview.setObjectName("preview.layout")
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(self.design_preview)
        self.layout_inspector = Inspector(self.controller, self.safe_command)
        self.layout_tree.selected_id.connect(lambda ident: self.select_object("ui", ident))
        self.design_preview.selected.connect(lambda ident: self.select_object("ui", ident))
        splitter.addWidget(outline)
        splitter.addWidget(scroll)
        splitter.addWidget(self.layout_inspector)
        splitter.setSizes([240, 920, 300])
        splitter.setStretchFactor(1, 1)
        self.pages.addWidget(splitter)

    def safe_command(self, op, args):
        """Report GUI errors without hiding them from generic control API callers."""
        try:
            result = self.controller.command(op, args)
            self._last_result = result
            return result
        except Exception as exc:
            self._last_result = {"error": str(exc)}
            self.show_notice("error", str(exc))
            return self._last_result

    def refresh(self, kind):
        """Rebind representations only when design or runtime instances have changed."""
        c = self.controller
        self.setWindowTitle(f"{c.project['name']} — COMTool-x")
        self.project_label.setText(c.project["name"] + (" *" if c.dirty else ""))
        if kind in ("project", "plugins"):
            if self.library.preview:
                self.library.preview.hide()
            self.library.clear()
            categories = {}
            for definition in c.registry.nodes.values():
                parent = categories.get(definition.category)
                if parent is None:
                    parent = QTreeWidgetItem([definition.category])
                    self.library.addTopLevelItem(parent)
                    categories[definition.category] = parent
                item = QTreeWidgetItem([definition.name])
                item.setData(0, Qt.ItemDataRole.UserRole, definition.id)
                parent.addChild(item)
            self.library.expandAll()
            self.wire_combo.clear()
            for definition in c.registry.wires.values():
                self.wire_combo.addItem(definition.name, definition.id)
            self.container_menu.clear()
            for definition in c.registry.containers.values():
                self.container_menu.addAction(definition.name, lambda checked=False, ident=definition.id:
                                              self.add_container(ident))
        if kind in ("project", "graph", "plugins", "runtime", "mode"):
            self.graph.synchronize()
        if kind in ("project", "graph", "plugins", "layout", "theme", "mode"):
            self.layout_tree.synchronize()
            self.design_preview.synchronize()
            self.runtime_preview.synchronize()
            self.view_combo.clear()
            for node in c.project["nodes"]:
                definition = c.registry.nodes.get(node["plugin"])
                if definition:
                    for view in definition.views:
                        self.view_combo.addItem(f"{node['name']} / {view}", (node["id"], view))
        if kind in ("project", "theme"):
            apply_theme(QApplication.instance(), c.theme)
        if kind == "project":
            self.set_workspace("runtime" if c.design_locked else "graph")
            QTimer.singleShot(0, self.graph.fit_content)
        self.update_button.setEnabled(not c.design_locked)
        self.mode_button.setText("进入编辑" if c.design_locked else "进入运行只读")
        if self.selected_object:
            try:
                self.select_object(*self.selected_object)
            except (ValueError, StopIteration):
                self.selected_object = None
                self.graph_inspector.show_empty()
                self.layout_inspector.show_empty()
        self.poll_status()

    def poll_status(self):
        engine = self.controller.engine
        self.start_button.setEnabled(not engine.running)
        self.stop_button.setEnabled(engine.running)
        mode = "设计只读" if self.controller.design_locked else "设计可编辑"
        self.status_label.setText(f"{mode}　{'运行中' if engine.running else '已停止'}"
                                  f"　处理 {engine.processed:,}　错误 {engine.failed:,}")
        if engine.events:
            event = engine.events[-1]
            self.statusBar().showMessage(event["message"])

    def select_object(self, kind, ident):
        self.selected_object = (kind, ident)
        if kind == "ui":
            self.design_preview.select(ident)
            self.layout_inspector.show_object(kind, ident)
        else:
            self.graph_inspector.show_object(kind, ident)

    def set_workspace(self, name):
        if name not in ("graph", "layout", "runtime"):
            raise WorkbenchError("workspace 需要 graph、layout 或 runtime")
        self.current_workspace = name
        self.pages.setCurrentIndex(("graph", "layout", "runtime").index(name))
        for key, button in self.workspace_buttons.items():
            button.setChecked(key == name)

    def add_library_node(self, item, column=0):
        ident = item.data(0, Qt.ItemDataRole.UserRole)
        if ident:
            center = self.graph.mapToScene(self.graph.viewport().rect().center())
            self.safe_command("graph.node.add", {"plugin": ident, "position": [center.x(), center.y()]})

    def filter_library(self, text):
        for index in range(self.library.topLevelItemCount()):
            parent = self.library.topLevelItem(index)
            visible = False
            for child_index in range(parent.childCount()):
                child = parent.child(child_index)
                match = text.lower() in (child.text(0) + child.data(0, Qt.ItemDataRole.UserRole)).lower()
                child.setHidden(not match)
                visible |= match
            parent.setHidden(not visible)

    def parent_for_new_ui(self):
        if self.selected_object and self.selected_object[0] == "ui":
            item = ui_by_id(self.controller.project, self.selected_object[1])
            return item["id"] if item.get("plugin") else item["parent"]
        return self.controller.project["ui"]["root"]

    def add_container(self, plugin):
        definition = self.controller.registry.containers[plugin]
        self.safe_command("ui.item.add", {"plugin": plugin, "parent": self.parent_for_new_ui(),
            "title": definition.name, "properties": {key: spec["default"]
                                                   for key, spec in definition.properties.items()}})

    def add_existing_view(self):
        value = self.view_combo.currentData()
        if value:
            node, view = value
            self.safe_command("ui.item.add", {"node": node, "view": view, "parent": self.parent_for_new_ui()})

    def remove_ui_item(self):
        if self.selected_object and self.selected_object[0] == "ui":
            self.safe_command("ui.item.remove", {"id": self.selected_object[1]})

    def update_ui(self):
        result = self.safe_command("ui.update", {})
        if "error" not in result:
            self.set_workspace("layout")
            self.show_notice("info", f"新增 {len(result['added'])} 个视图；失效绑定 {len(result['issues'])} 项")

    def confirm_unsaved(self):
        if not self.controller.dirty:
            return True
        reply = QMessageBox.question(self, "未保存的工程", "保存当前设计后再继续？",
            QMessageBox.StandardButton.Save | QMessageBox.StandardButton.Discard |
            QMessageBox.StandardButton.Cancel)
        if reply == QMessageBox.StandardButton.Cancel:
            return False
        return self.save_project(False) if reply == QMessageBox.StandardButton.Save else True

    def new_project(self):
        if self.confirm_unsaved():
            self.safe_command("project.new", {})

    def open_project(self, read_only):
        if not self.confirm_unsaved():
            return
        path, _ = QFileDialog.getOpenFileName(self, "打开 COMTool-x 工程", str(self.controller.project_dir),
            "COMTool-x 工程 (*.comtoolx)", options=QFileDialog.Option.DontUseNativeDialog)
        if path:
            self.safe_command("project.open", {"path": path, "read_only": read_only})

    def save_project(self, save_as):
        path = self.controller.path
        if save_as or not path:
            dialog = QFileDialog(self, "保存工程",
                path or str(self.controller.project_dir / f"{self.controller.project['name']}.comtoolx"),
                "COMTool-x 工程 (*.comtoolx)")
            dialog.setOption(QFileDialog.Option.DontUseNativeDialog)
            dialog.setAcceptMode(QFileDialog.AcceptMode.AcceptSave)
            # Set the suffix before accepting so overwrite checks use the final filename.
            dialog.setDefaultSuffix("comtoolx")
            if dialog.exec() != QDialog.DialogCode.Accepted:
                return False
            path = dialog.selectedFiles()[0]
        if not path:
            return False
        return "error" not in self.safe_command("project.save", {"path": path})

    def rename_project(self):
        name, accepted = QInputDialog.getText(self, "工程名称", "名称",
                                              text=self.controller.project["name"])
        if accepted:
            self.safe_command("project.patch", {"name": name})

    def load_plugin(self):
        path, _ = QFileDialog.getOpenFileName(self, "加载本地插件", "", "Python 插件 (*.py)",
                                              options=QFileDialog.Option.DontUseNativeDialog)
        if path:
            self.safe_command("plugin.load", {"path": path})

    def show_json(self, title, data):
        dialog = QDialog(self)
        dialog.setWindowTitle(title)
        dialog.resize(800, 600)
        layout = QVBoxLayout(dialog)
        text = QPlainTextEdit(json.dumps(data, ensure_ascii=False, indent=2))
        text.setReadOnly(True)
        layout.addWidget(text)
        close = QDialogButtonBox(QDialogButtonBox.StandardButton.Close)
        close.rejected.connect(dialog.reject)
        layout.addWidget(close)
        dialog.show()
        dialog.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)

    def show_registry(self):
        self.show_json("插件契约", self.controller.registry.describe())

    def show_api(self):
        self.show_json("本地 API 与 Skill", {"connection": self.api_info, "commands": COMMANDS,
            "skill": "skills/comtool-x/SKILL.md", "transport": "HTTP /v1/command"})

    def show_validation(self):
        self.show_json("工程检查", self.controller.command("project.validate", {}))

    def show_notice(self, level, message):
        self.statusBar().showMessage(message, 15000)
        if level == "error":
            self.controller.engine.record("error", message)

    def _objects(self):
        """Name internal Qt controls, including application-owned modal dialogs."""
        result = {}
        for top_index, top in enumerate(QApplication.topLevelWidgets()):
            if (top is not self and top.windowType() == Qt.WindowType.ToolTip
                    and top is not self.library.preview):
                continue
            prefix = top.objectName() or f"dialog.{type(top).__name__}.{top_index}"
            for index, obj in enumerate([top, *top.findChildren(QWidget), *top.findChildren(QAction)]):
                name = obj.objectName()
                if not name:
                    name = f"{prefix}/{type(obj).__name__}.{index}"
                    obj.setObjectName(name)
                result[name] = obj
        return result

    def _value(self, obj):
        if isinstance(obj, QCheckBox):
            return obj.isChecked()
        if isinstance(obj, (QSpinBox, QDoubleSpinBox, QSlider)):
            return obj.value()
        if isinstance(obj, QComboBox):
            return obj.currentText()
        if isinstance(obj, (QLineEdit, QLabel, QAbstractButton)):
            return obj.text()
        if isinstance(obj, (QPlainTextEdit, QTextEdit)):
            return obj.toPlainText()
        if isinstance(obj, QAction):
            return obj.text()
        return None

    def automation(self, op, args):
        """Internal rendering and structured Qt operations never use OS mouse/keyboard."""
        if op == "workspace.set":
            self.set_workspace(args["view"])
            return {"view": self.current_workspace}
        if op == "graph.fit":
            self.graph.fit_content()
            return {"fitted": True}
        if op == "graph.overlaps":
            return {"overlaps": json_value(self.graph.overlaps)}
        if op == "library.preview":
            return self.library.show_preview(args["plugin"],
                self.library.viewport().mapToGlobal(QPoint(self.library.viewport().width(), 0)))
        if op == "capture":
            return self.capture(args)
        if op == "view.actions":
            return {f"{builder.namespace}/{ident}": list(getattr(widget, "api_actions", {}))
                    for builder in (self.design_preview, self.runtime_preview)
                    for ident, widget in builder.views.items()}
        if op == "view.call":
            ident = args["id"]
            namespace, _, short_id = ident.partition("/")
            builder = self.design_preview if namespace == "layout" else self.runtime_preview
            widget = builder.views.get(short_id or ident)
            action = getattr(widget, "api_actions", {}).get(args["action"]) if widget else None
            if action is None:
                raise WorkbenchError("视图或专用操作不存在，请先调用 view.actions")
            return json_value(action(args.get("args", {})))
        objects = self._objects()
        if op == "controls.list":
            root = args.get("root", "")
            return [{"id": name, "class": type(obj).__name__, "value": self._value(obj),
                     "enabled": obj.isEnabled(), "visible": obj.isVisible(),
                     "read_only": bool(obj.isReadOnly()) if hasattr(obj, "isReadOnly") else False,
                     "options": [obj.itemText(i) for i in range(obj.count())] if isinstance(obj, QComboBox) else None}
                    for name, obj in objects.items() if name.startswith(root)]
        obj = objects.get(args["id"])
        if obj is None:
            raise WorkbenchError("控件 ID 不存在；请重新发现 controls.list")
        if not obj.isEnabled():
            raise WorkbenchError("控件当前禁用")
        if op == "controls.click":
            if isinstance(obj, QAction):
                QTimer.singleShot(0, obj.trigger)
            elif isinstance(obj, QAbstractButton):
                QTimer.singleShot(0, obj.click)
            else:
                raise WorkbenchError("该对象不是可点击按钮或动作")
            return {"scheduled": True, "id": args["id"]}
        if op == "controls.set":
            value = args["value"]
            self._last_result = None
            if hasattr(obj, "isReadOnly") and obj.isReadOnly():
                raise WorkbenchError("控件当前只读")
            if isinstance(obj, QCheckBox):
                obj.setChecked(bool(value))
            elif isinstance(obj, (QSpinBox, QSlider)):
                obj.setValue(int(value))
            elif isinstance(obj, QDoubleSpinBox):
                obj.setValue(float(value))
            elif isinstance(obj, QComboBox):
                index = obj.findText(str(value))
                if index < 0 and not obj.isEditable():
                    raise WorkbenchError("下拉选项不存在")
                obj.setCurrentText(str(value))
            elif isinstance(obj, QLineEdit):
                obj.setText(str(value))
                if args.get("submit"):
                    obj.returnPressed.emit()
            elif isinstance(obj, (QPlainTextEdit, QTextEdit)):
                obj.setPlainText(str(value))
            else:
                raise WorkbenchError("该对象没有标准可写值；请使用插件操作")
            if isinstance(self._last_result, dict) and self._last_result.get("error"):
                raise WorkbenchError(self._last_result["error"])
            return {"value": self._value(obj), "operation": self._last_result}
        raise WorkbenchError(f"不支持的控件操作：{op}")

    def capture(self, args):
        """Capture a widget or full graph scene directly into a PNG paint device."""
        target = args.get("target", "window")
        directory = self.controller.data_dir / "screenshots"
        directory.mkdir(parents=True, exist_ok=True)
        path = directory / f"{uuid.uuid4().hex}.png"
        if target == "graph" and args.get("full", True):
            bounds = self.graph.scene().itemsBoundingRect().adjusted(-35, -35, 35, 35)
            if bounds.width() < 100 or bounds.height() < 100:
                bounds = QRectF(0, 0, 1000, 650)
            width = min(4096, max(320, int(args.get("width", 1600))))
            height = min(4096, max(240, round(width * bounds.height() / bounds.width())))
            image = QImage(width, height, QImage.Format.Format_ARGB32_Premultiplied)
            image.fill(QColor("#161E29"))
            painter = QPainter(image)
            self.graph.scene().render(painter, QRectF(0, 0, width, height), bounds,
                                      Qt.AspectRatioMode.KeepAspectRatio)
            painter.end()
        else:
            if target in ("layout", "runtime"):
                self.set_workspace(target)
                widget = self.design_preview if target == "layout" else self.runtime_preview
                self.pages.layout().activate()
                widget.ensurePolished()
                if widget.layout():
                    widget.layout().activate()
            elif target == "window":
                widget = self
            else:
                widget = self._objects().get(target)
                if widget is None:
                    widget = self.runtime_preview.frames.get(target) or self.design_preview.frames.get(target)
                if not isinstance(widget, QWidget):
                    raise WorkbenchError("截图目标不是有效的界面对象")
            self.poll_status()
            for child in [widget, *widget.findChildren(QWidget)]:
                refresh = getattr(child, "refresh_from_runtime", None)
                if callable(refresh):
                    refresh()
            image = widget.grab().toImage()
        if image.isNull() or not image.save(str(path), "PNG"):
            raise WorkbenchError("内部截图渲染失败")
        return {"path": str(path), "width": image.width(), "height": image.height(),
                "target": target, "url": f"/v1/screenshots/{path.name}"}

    def closeEvent(self, event):
        if not getattr(self, "discard_on_close", False) and not self.confirm_unsaved():
            event.ignore()
            return
        self.controller.close()
        event.accept()
