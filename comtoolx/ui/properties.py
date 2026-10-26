"""Schema-driven inspectors. Plugin fields stay discoverable by humans and AI."""

from __future__ import annotations

import json

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QWidget, QVBoxLayout, QFormLayout, QLabel, QLineEdit,
                              QPlainTextEdit, QCheckBox, QSpinBox, QDoubleSpinBox,
                              QComboBox, QPushButton, QScrollArea)

from ..model import node_by_id, ui_by_id


class Inspector(QScrollArea):
    """Edit declared properties and commit one real model transaction per apply."""

    def __init__(self, controller, request):
        super().__init__()
        self.controller, self.request = controller, request
        self.setWidgetResizable(True)
        self.setMinimumWidth(240)
        self.setObjectName("editor.inspector")
        self.current = None
        self.show_empty()

    def begin(self, title, detail):
        previous = self.widget()
        if previous:
            previous.deleteLater()
        widget = QWidget()
        layout = QVBoxLayout(widget)
        heading = QLabel(title)
        heading.setProperty("role", "heading")
        layout.addWidget(heading)
        description = QLabel(detail)
        description.setWordWrap(True)
        description.setProperty("role", "muted")
        layout.addWidget(description)
        form = QFormLayout()
        form.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.AllNonFixedFieldsGrow)
        layout.addLayout(form)
        self.setWidget(widget)
        return widget, layout, form

    def show_empty(self):
        _, layout, _ = self.begin("属性", "选择节点、导线或界面元素，查看并编辑其属性。")
        layout.addStretch()
        self.current = None

    def field(self, form, key, spec, value):
        kind = spec.get("type", "string")
        if "choices" in spec:
            editor = QComboBox()
            for choice in spec["choices"]:
                editor.addItem(str(choice), choice)
            editor.setCurrentIndex(max(0, editor.findData(value)))
        elif kind == "boolean":
            editor = QCheckBox()
            editor.setChecked(bool(value))
        elif kind == "integer":
            editor = QSpinBox()
            editor.setRange(spec.get("min", -2147483647), spec.get("max", 2147483647))
            editor.setValue(int(value or 0))
        elif kind == "number":
            editor = QDoubleSpinBox()
            editor.setDecimals(8)
            editor.setRange(spec.get("min", -1e15), spec.get("max", 1e15))
            editor.setValue(float(value or 0))
        elif kind == "json" or spec.get("multiline"):
            editor = QPlainTextEdit()
            editor.setPlainText(json.dumps(value, ensure_ascii=False, indent=2)
                                if kind == "json" else str(value or ""))
            editor.setMinimumHeight(95 if kind == "json" else 150)
        else:
            editor = QLineEdit(str(value if value is not None else ""))
            if spec.get("secret"):
                editor.setEchoMode(QLineEdit.EchoMode.Password)
        editor.setObjectName(f"property.{key}")
        editor.setToolTip(spec.get("description", key))
        form.addRow(spec.get("label", key), editor)
        return editor

    def value(self, editor, spec):
        if isinstance(editor, QCheckBox):
            return editor.isChecked()
        if isinstance(editor, (QSpinBox, QDoubleSpinBox)):
            return editor.value()
        if isinstance(editor, QComboBox):
            return editor.currentData()
        if isinstance(editor, QPlainTextEdit):
            return json.loads(editor.toPlainText()) if spec.get("type") == "json" else editor.toPlainText()
        return editor.text()

    def show_object(self, kind, ident):
        self.current = (kind, ident)
        if kind == "node":
            node = node_by_id(self.controller.project, ident)
            definition = self.controller.registry.nodes.get(node["plugin"])
            if definition is None:
                _, layout, _ = self.begin("缺少插件", node["plugin"])
                layout.addStretch()
                return
            widget, layout, form = self.begin(node["name"], f"{definition.id}\n{definition.description}\n{ident}")
            name = self.field(form, "name", {"label": "名称"}, node["name"])
            editors = {key: self.field(form, key, spec, node["config"].get(key, spec.get("default")))
                       for key, spec in definition.properties.items()}
            apply = QPushButton("应用节点配置")
            apply.setObjectName("property.apply")
            def commit():
                try:
                    config = {key: self.value(editors[key], spec) for key, spec in definition.properties.items()}
                    self.request("graph.node.patch", {"id": ident, "name": name.text(), "config": config})
                except Exception as exc:
                    self.controller.notice.emit("error", str(exc))
            apply.clicked.connect(commit)
            apply.setEnabled(not self.controller.design_locked and not self.controller.engine.running)
            layout.addWidget(apply)
            ports = "\n".join(f"{p.label}  {p.direction}  {p.data_type}" for p in definition.ports)
            label = QLabel(ports or "无数据端口")
            label.setWordWrap(True)
            label.setProperty("role", "muted")
            layout.addWidget(label)
        elif kind == "wire":
            wire = next(item for item in self.controller.project["wires"] if item["id"] == ident)
            definition = self.controller.registry.wires[wire["plugin"]]
            widget, layout, form = self.begin(definition.name,
                f"{wire['source']['node']}.{wire['source']['port']}\n→ {wire['target']['node']}.{wire['target']['port']}")
            editors = {key: self.field(form, key, spec, wire.get("properties", {}).get(key, spec.get("default")))
                       for key, spec in definition.properties.items()}
            apply = QPushButton("应用导线属性")
            apply.setObjectName("property.apply")
            apply.clicked.connect(lambda: self.request("graph.wire.patch", {
                "id": ident, "properties": {key: self.value(editors[key], spec)
                                          for key, spec in definition.properties.items()}}))
            apply.setEnabled(not self.controller.design_locked and not self.controller.engine.running)
            layout.addWidget(apply)
        else:
            item = ui_by_id(self.controller.project, ident)
            widget, layout, form = self.begin("界面属性", ident)
            title = self.field(form, "title", {"label": "标题"}, item.get("title", ""))
            stretch = self.field(form, "stretch", {"type": "integer", "min": 0, "max": 100,
                                                  "label": "拉伸权重"}, item.get("stretch", 1))
            props = self.field(form, "properties", {"type": "json", "label": "布局 / 视图属性"},
                               item.get("properties", {}))
            if item.get("plugin"):
                definition = self.controller.registry.containers.get(item["plugin"])
                detail = QLabel(f"容器：{definition.name if definition else item['plugin']}\n"
                                "spacing/margin 控制间距；网格支持 columns、row/column。")
            else:
                detail = QLabel(f"绑定：{item.get('node')} / {item.get('view')}\n"
                                "可用属性：min_width/min_height/max_width/max_height、label。")
            detail.setWordWrap(True)
            layout.addWidget(detail)
            parent = QComboBox()
            parent.setObjectName("property.parent")
            for candidate in self.controller.project["ui"]["items"]:
                if candidate.get("plugin") and candidate["id"] != ident:
                    parent.addItem(candidate.get("title") or candidate["id"], candidate["id"])
            parent.setCurrentIndex(max(0, parent.findData(item.get("parent"))))
            if ident != self.controller.project["ui"]["root"]:
                form.addRow("父容器", parent)
            apply = QPushButton("应用界面属性")
            apply.setObjectName("property.apply")
            def apply_ui():
                try:
                    properties = json.loads(props.toPlainText())
                    commands = [{"op": "ui.item.patch", "args": {"id": ident, "title": title.text(),
                                "stretch": stretch.value(), "properties": properties}}]
                    if ident != self.controller.project["ui"]["root"] and parent.currentData() != item.get("parent"):
                        commands.append({"op": "ui.item.move", "args": {"id": ident, "parent": parent.currentData()}})
                    self.request("batch", {"commands": commands})
                except Exception as exc:
                    self.controller.notice.emit("error", str(exc))
            apply.clicked.connect(apply_ui)
            apply.setEnabled(not self.controller.design_locked)
            layout.addWidget(apply)
        layout.addStretch()
