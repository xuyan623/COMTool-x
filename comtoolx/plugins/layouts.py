"""Adaptive row, column, grid and tab containers supplied as layout plugins."""

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QGridLayout, QTabWidget

from ..sdk import ContainerDefinition


def make_box(orientation, item, children):
    """Use stretch weights and minimum sizes rather than fixed screen coordinates."""
    widget = QWidget()
    layout = QHBoxLayout(widget) if orientation == "row" else QVBoxLayout(widget)
    props = item.get("properties", {})
    margin = int(props.get("margin", 8))
    layout.setContentsMargins(margin, margin, margin, margin)
    layout.setSpacing(int(props.get("spacing", 10)))
    for child, child_widget in children:
        layout.addWidget(child_widget, max(0, int(child.get("stretch", 1))))
    return widget


def make_grid(item, children):
    """Place children in editable grid cells, with automatic row-major defaults."""
    widget = QWidget()
    layout = QGridLayout(widget)
    props = item.get("properties", {})
    columns = max(1, int(props.get("columns", 2)))
    margin = int(props.get("margin", 8))
    layout.setContentsMargins(margin, margin, margin, margin)
    layout.setSpacing(int(props.get("spacing", 10)))
    for index, (child, child_widget) in enumerate(children):
        placement = child.get("properties", {})
        row = max(0, int(placement.get("row", index // columns)))
        column = max(0, int(placement.get("column", index % columns)))
        layout.addWidget(child_widget, row, column,
                         max(1, int(placement.get("rowspan", 1))),
                         max(1, int(placement.get("colspan", 1))))
        layout.setColumnStretch(column, max(0, int(child.get("stretch", 1))))
        layout.setRowStretch(row, max(0, int(child.get("stretch", 1))))
    return widget


def make_tabs(item, children):
    """Tabs keep each child's view alive while changing only its presentation."""
    tabs = QTabWidget()
    for child, child_widget in children:
        tabs.addTab(child_widget, child.get("title", child["id"]))
    return tabs


def register(registry):
    properties = {
        "spacing": {"type": "integer", "default": 10, "min": 0, "max": 64},
        "margin": {"type": "integer", "default": 8, "min": 0, "max": 64},
    }
    registry.register(ContainerDefinition("layout.row", "横向分区", properties,
                       lambda item, children: make_box("row", item, children)))
    registry.register(ContainerDefinition("layout.column", "纵向分区", properties,
                       lambda item, children: make_box("column", item, children)))
    registry.register(ContainerDefinition("layout.grid", "网格分区",
                       {**properties, "columns": {"type": "integer", "default": 2,
                                                  "min": 1, "max": 12}}, make_grid))
    registry.register(ContainerDefinition("layout.tabs", "标签页", {}, make_tabs))
