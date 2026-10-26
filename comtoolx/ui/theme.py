"""Deliberate desktop tokens: quiet chrome, readable controls, data-led plot colors."""

import os
import sys
from pathlib import Path
from PySide6.QtGui import QFont, QFontDatabase, QPalette, QColor

DARK = {
    "window": "#161B22", "panel": "#202833", "input": "#18212D",
    "text": "#E4EDF6", "muted": "#A2B3C7", "line": "#344253",
    "accent": "#78B9F2", "selected": "#234D73", "hover": "#2B3B4E",
}
LIGHT = {
    "window": "#F3F6F9", "panel": "#FFFFFF", "input": "#FFFFFF",
    "text": "#1B2B3D", "muted": "#50667C", "line": "#D6E0EA",
    "accent": "#176BC1", "selected": "#DFEEFC", "hover": "#EAF1F8",
}


# OFFSCREEN FONT SUPPORT BEGIN: Qt's offscreen Windows platform has no native
# font database. Load installed Windows fonts only when that database is empty.
# Scope: internal/offscreen rendering, no fonts are copied or redistributed.
# Remove when Qt's offscreen platform enumerates Windows fonts itself.
def prepare_fonts():
    if sys.platform == "win32" and not QFontDatabase.families():
        fonts = Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts"
        for name in ("msyh.ttc", "segoeui.ttf", "consola.ttf"):
            path = fonts / name
            if path.exists():
                QFontDatabase.addApplicationFont(str(path))
# OFFSCREEN FONT SUPPORT END


def apply_theme(application, name):
    """Use one token set across the shell and plugin forms; native plots own their pens."""
    c = DARK if name == "dark" else LIGHT
    application.setStyle("Fusion")
    application.setFont(QFont("Microsoft YaHei UI", 9))
    palette = application.palette()
    for role, color in ((QPalette.ColorRole.Window, "window"),
                        (QPalette.ColorRole.Base, "input"),
                        (QPalette.ColorRole.Button, "panel"),
                        (QPalette.ColorRole.Text, "text"),
                        (QPalette.ColorRole.WindowText, "text"),
                        (QPalette.ColorRole.ButtonText, "text"),
                        (QPalette.ColorRole.Highlight, "selected")):
        palette.setColor(role, QColor(c[color]))
    application.setPalette(palette)
    application.setStyleSheet(f"""
        QWidget {{ color: {c['text']}; }}
        QMainWindow, QStackedWidget {{ background: {c['window']}; }}
        QLabel {{ background: transparent; }}
        QLabel[role="heading"] {{ font-size: 17px; font-weight: 600; padding: 4px 0; }}
        QLabel[role="panelTitle"] {{ font-weight: 600; color: {c['text']}; padding: 2px; }}
        QLabel[role="muted"] {{ color: {c['muted']}; }}
        QLabel[role="value"] {{ font-size: 24px; font-weight: 600; }}
        QFrame[role="panel"] {{ background: {c['panel']}; border: 1px solid {c['line']}; border-radius: 8px; }}
        QFrame[role="container"] {{ background: transparent; border: 1px solid transparent; border-radius: 6px; }}
        QFrame[selected="true"] {{ border: 2px solid {c['accent']}; }}
        QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox, QDoubleSpinBox {{
            background: {c['input']}; border: 1px solid {c['line']}; border-radius: 5px;
            padding: 5px 7px; min-height: 22px; selection-background-color: {c['selected']};
        }}
        QLineEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus {{
            border: 1px solid {c['accent']};
        }}
        QPushButton, QToolButton {{
            background: {c['panel']}; border: 1px solid {c['line']}; border-radius: 6px;
            padding: 6px 12px; min-height: 20px;
        }}
        QPushButton:hover, QToolButton:hover {{ background: {c['hover']}; border-color: {c['accent']}; }}
        QPushButton:pressed, QToolButton:pressed, QToolButton:checked {{ background: {c['selected']}; }}
        QPushButton[primary="true"] {{ background: {c['selected']}; border-color: {c['accent']}; }}
        QWidget:disabled {{ color: #7D8C9F; }}
        QPushButton:disabled {{ border-color: {c['line']}; }}
        QTreeWidget, QTableWidget, QListWidget {{
            background: {c['window']}; border: none; outline: none; alternate-background-color: {c['panel']};
        }}
        QTreeWidget::item {{ padding: 5px 3px; min-height: 23px; }}
        QTreeWidget::item:selected, QTableWidget::item:selected {{ background: {c['selected']}; }}
        QHeaderView::section {{ background: {c['panel']}; color: {c['muted']};
            border: none; border-bottom: 1px solid {c['line']}; padding: 6px; }}
        QMenuBar {{ background: {c['window']}; padding: 3px; }}
        QMenuBar::item:selected, QMenu::item:selected {{ background: {c['selected']}; }}
        QMenu {{ background: {c['panel']}; border: 1px solid {c['line']}; padding: 5px; }}
        QMenu::item {{ padding: 7px 25px; }}
        QToolBar {{ border: none; spacing: 6px; padding: 8px; background: {c['window']}; }}
        QStatusBar {{ border-top: 1px solid {c['line']}; color: {c['muted']}; }}
        QSplitter::handle {{ background: {c['line']}; width: 1px; height: 1px; }}
        QScrollArea {{ border: none; }}
        QScrollBar:vertical {{ background: {c['window']}; width: 10px; margin: 0; }}
        QScrollBar::handle:vertical {{ background: {c['line']}; border-radius: 4px; min-height: 30px; }}
        QScrollBar:horizontal {{ background: {c['window']}; height: 10px; margin: 0; }}
        QScrollBar::handle:horizontal {{ background: {c['line']}; border-radius: 4px; min-width: 30px; }}
        QScrollBar::add-line, QScrollBar::sub-line {{ width: 0; height: 0; }}
        QTabWidget::pane {{ border: 1px solid {c['line']}; border-radius: 5px; }}
        QTabBar::tab {{ background: {c['panel']}; padding: 8px 15px; }}
        QTabBar::tab:selected {{ background: {c['selected']}; }}
        QCheckBox {{ spacing: 6px; }}
        QToolTip {{ background: {c['panel']}; color: {c['text']}; border: 1px solid {c['line']}; padding: 6px; }}
    """)
