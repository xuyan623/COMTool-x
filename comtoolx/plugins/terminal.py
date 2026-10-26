"""Native ANSI/VT terminal backed by pyte, with keyboard, history and selection."""

from __future__ import annotations

import codecs

import pyte

from ..sdk import Definition, NodeRuntime, Port, WorkbenchError


class Terminal(NodeRuntime):
    def __init__(self, node, engine):
        super().__init__(node, engine)
        self.screen = pyte.HistoryScreen(self.config["columns"], self.config["rows"],
                                         history=self.config["history_lines"])
        self.stream = pyte.Stream(self.screen)
        self.decoder = codecs.getincrementaldecoder(self.config["encoding"])("replace")
        self.version = 0
        self.state.update(columns=self.config["columns"], rows=self.config["rows"])

    def receive(self, port, data):
        with self.lock:
            self.stream.feed(self.decoder.decode(data))
            self.version += 1
        return self.emit("through", data)

    def screen_data(self):
        with self.lock:
            return self.version, [[self.screen.buffer[y][x] for x in range(self.screen.columns)]
                                  for y in range(self.screen.lines)], (
                                      self.screen.cursor.x, self.screen.cursor.y)

    def action(self, name, args):
        if name == "send":
            data = bytes.fromhex(args["hex"]) if "hex" in args else str(
                args.get("text", "")).encode(self.config["encoding"])
            return self.emit("tx", data).result(timeout=15)
        with self.lock:
            if name == "resize":
                columns, rows = max(2, int(args["columns"])), max(2, int(args["rows"]))
                self.screen.resize(lines=rows, columns=columns)
                self.state.update(columns=columns, rows=rows)
                self.version += 1
                if self.engine.running:
                    self.emit("resize", {"columns": columns, "rows": rows})
                return self.state.copy()
            if name == "read_screen":
                return {"lines": list(self.screen.display), "cursor": [
                    self.screen.cursor.x, self.screen.cursor.y]}
            if name == "clear":
                self.screen.reset()
                self.version += 1
                return {"cleared": True}
            if name in ("previous_page", "next_page"):
                if name == "previous_page":
                    self.screen.prev_page()
                else:
                    self.screen.next_page()
                self.version += 1
                return {"page_changed": True}
        return super().action(name, args)


def terminal_view(runtime, request, properties):
    from PySide6.QtCore import Qt, QTimer, QPoint
    from PySide6.QtGui import QColor, QFont, QFontMetricsF, QPainter, QGuiApplication
    from PySide6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QPushButton
    palette = {"default": "#DEE8F3", "black": "#17202B", "red": "#F07878",
               "green": "#71CFA0", "brown": "#DFC078", "blue": "#79B5F4",
               "magenta": "#DDA0E8", "cyan": "#72C5D3", "white": "#DEE8F3",
               "brightblack": "#7C8EA3", "brightred": "#FF9B93",
               "brightgreen": "#A0ECC2", "brightyellow": "#FCE5A2",
               "brightblue": "#A5CEFF", "brightmagenta": "#F1BAF7",
               "brightcyan": "#AAEBF5", "brightwhite": "#FFFFFF"}

    class Canvas(QWidget):
        """Paint terminal cells directly; pyte holds all terminal parsing state."""

        def __init__(self):
            super().__init__()
            self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
            self.setFont(QFont("Consolas", 11))
            metrics = QFontMetricsF(self.font())
            self.cell_w, self.cell_h = metrics.horizontalAdvance("M"), metrics.height()
            self.baseline = metrics.ascent()
            self.cells = []
            self.cursor = (0, 0)
            self.version = -1
            self.selection_start = self.selection_end = None
            self.setMinimumSize(200, 120)
            self.setObjectName(f"{runtime.id}.terminal.canvas")

        def paintEvent(self, event):
            painter = QPainter(self)
            painter.fillRect(self.rect(), QColor("#101724"))
            painter.setFont(self.font())
            selected = sorted([self.selection_start, self.selection_end]) if (
                self.selection_start is not None and self.selection_end is not None) else None
            for y, row in enumerate(self.cells):
                # Draw adjacent cells with identical style as one run.
                x = 0
                while x < len(row):
                    cell = row[x]
                    point = (y, x)
                    chosen = selected is not None and selected[0] <= point <= selected[1]
                    end = x + 1
                    while end < len(row):
                        other = row[end]
                        other_chosen = selected is not None and selected[0] <= (y, end) <= selected[1]
                        if (other.fg, other.bg, other.bold, other.italics, other.underscore, other.reverse,
                            other_chosen) != (cell.fg, cell.bg, cell.bold, cell.italics, cell.underscore,
                                              cell.reverse, chosen):
                            break
                        end += 1
                    foreground = palette.get(cell.fg, "#" + cell.fg)
                    background = "#101724" if cell.bg == "default" else palette.get(cell.bg, "#" + cell.bg)
                    if cell.reverse:
                        foreground, background = background, foreground
                    if chosen:
                        background = "#315F8C"
                    painter.fillRect(int(x * self.cell_w), int(y * self.cell_h),
                                     int((end - x) * self.cell_w + 1), int(self.cell_h + 1), QColor(background))
                    font = QFont(self.font())
                    font.setBold(cell.bold)
                    font.setItalic(cell.italics)
                    font.setUnderline(cell.underscore)
                    painter.setFont(font)
                    painter.setPen(QColor(foreground))
                    text = "".join(row[index].data for index in range(x, end))
                    painter.drawText(int(x * self.cell_w), int(y * self.cell_h + self.baseline), text)
                    x = end
            if self.hasFocus():
                x, y = self.cursor
                painter.fillRect(int(x * self.cell_w), int((y + 1) * self.cell_h - 2),
                                 int(self.cell_w), 2, QColor("#73B9F4"))

        def resizeEvent(self, event):
            if properties.get("design") or not self.isVisible():
                return
            columns = max(2, int(self.width() / self.cell_w))
            rows = max(2, int(self.height() / self.cell_h))
            request("resize", {"columns": columns, "rows": rows})

        def showEvent(self, event):
            super().showEvent(event)
            self.resizeEvent(event)

        def selected_text(self):
            if self.selection_start is None or self.selection_end is None:
                return ""
            begin, end = sorted([self.selection_start, self.selection_end])
            result = []
            for y in range(begin[0], min(end[0] + 1, len(self.cells))):
                row = self.cells[y]
                first = begin[1] if y == begin[0] else 0
                last = end[1] + 1 if y == end[0] else len(row)
                result.append("".join(cell.data for cell in row[first:last]).rstrip())
            return "\n".join(result)

        def keyPressEvent(self, event):
            modifiers = event.modifiers()
            ctrl = bool(modifiers & Qt.KeyboardModifier.ControlModifier)
            if ctrl and event.key() == Qt.Key.Key_C and self.selected_text():
                QGuiApplication.clipboard().setText(self.selected_text())
                return
            if ctrl and event.key() == Qt.Key.Key_V:
                request("send", {"text": QGuiApplication.clipboard().text()})
                return
            keys = {Qt.Key.Key_Up: "\x1b[A", Qt.Key.Key_Down: "\x1b[B",
                    Qt.Key.Key_Right: "\x1b[C", Qt.Key.Key_Left: "\x1b[D",
                    Qt.Key.Key_Home: "\x1b[H", Qt.Key.Key_End: "\x1b[F",
                    Qt.Key.Key_Insert: "\x1b[2~", Qt.Key.Key_Delete: "\x1b[3~",
                    Qt.Key.Key_PageUp: "\x1b[5~", Qt.Key.Key_PageDown: "\x1b[6~",
                    Qt.Key.Key_Return: "\r", Qt.Key.Key_Enter: "\r",
                    Qt.Key.Key_Backspace: "\x7f", Qt.Key.Key_Tab: "\t", Qt.Key.Key_Escape: "\x1b"}
            for index, sequence in enumerate(("\x1bOP", "\x1bOQ", "\x1bOR", "\x1bOS",
                "\x1b[15~", "\x1b[17~", "\x1b[18~", "\x1b[19~", "\x1b[20~",
                "\x1b[21~", "\x1b[23~", "\x1b[24~")):
                keys[int(Qt.Key.Key_F1) + index] = sequence
            if ctrl and Qt.Key.Key_A <= event.key() <= Qt.Key.Key_Z:
                text = chr(event.key() - int(Qt.Key.Key_A) + 1)
            else:
                text = keys.get(event.key(), event.text())
            if text:
                self.selection_start = self.selection_end = None
                request("send", {"text": text})
            else:
                super().keyPressEvent(event)

        def cell_at(self, position):
            return (max(0, min(len(self.cells) - 1, int(position.y() / self.cell_h))),
                    max(0, int(position.x() / self.cell_w)))

        def mousePressEvent(self, event):
            self.selection_start = self.selection_end = self.cell_at(event.position())
            self.setFocus()
            self.update()

        def mouseMoveEvent(self, event):
            if event.buttons() & Qt.MouseButton.LeftButton:
                self.selection_end = self.cell_at(event.position())
                self.update()

        def wheelEvent(self, event):
            request("previous_page" if event.angleDelta().y() > 0 else "next_page", {})

    panel = QWidget()
    layout = QVBoxLayout(panel)
    bar = QHBoxLayout()
    clear, copy = QPushButton("清屏"), QPushButton("复制")
    clear.clicked.connect(lambda: request("clear", {}))
    canvas = Canvas()
    copy.clicked.connect(lambda: QGuiApplication.clipboard().setText(
        canvas.selected_text() or "\n".join(runtime.action("read_screen", {})["lines"])))
    bar.addWidget(clear)
    bar.addWidget(copy)
    bar.addStretch()
    layout.addLayout(bar)
    layout.addWidget(canvas, 1)
    def refresh():
        if runtime.version != canvas.version:
            canvas.version, canvas.cells, canvas.cursor = runtime.screen_data()
            canvas.update()
    timer = QTimer(panel)
    timer.timeout.connect(refresh)
    timer.start(20)
    refresh()
    panel.api_actions = {"read_screen": lambda args: runtime.action("read_screen", args)}
    panel.refresh_from_runtime = refresh
    return panel


def register(registry):
    registry.register(Definition("terminal.ansi", "交互终端", "调试",
        "ANSI/VT、颜色、历史滚动、复制选择、粘贴、控制键和 SSH 窗口尺寸",
        (Port("rx", "终端输入", "in", "bytes"), Port("tx", "键盘输出", "out", "bytes"),
         Port("through", "原始数据继续", "out", "bytes"), Port("resize", "终端尺寸", "out", "object")),
        {"encoding": {"type": "string", "default": "utf-8"},
         "columns": {"type": "integer", "default": 80, "min": 2, "max": 500},
         "rows": {"type": "integer", "default": 24, "min": 2, "max": 300},
         "history_lines": {"type": "integer", "default": 10000, "min": 100, "max": 100000}},
        Terminal, {"terminal": terminal_view},
        {"send": {"description": "发送 text 或 hex"},
         "resize": {"description": "设置 columns/rows", "immediate": True, "requires_running": False},
         "read_screen": {"description": "读取解析后的终端屏幕", "immediate": True, "requires_running": False},
         "clear": {"description": "清屏", "immediate": True, "requires_running": False},
         "previous_page": {"description": "历史上一页", "immediate": True, "requires_running": False},
         "next_page": {"description": "历史下一页", "immediate": True, "requires_running": False}}))
