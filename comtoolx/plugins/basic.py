"""Control, send/receive, timer and recording plugins with real runtime behaviour."""

from __future__ import annotations

import copy
import codecs
import json
import threading
import time
from collections import deque
from pathlib import Path

from ..sdk import Definition, NodeRuntime, Port, WorkbenchError
from .protocols import encode_text


class Control(NodeRuntime):
    """A UI view and API action share the same value checks and output publication."""

    def __init__(self, node, engine):
        super().__init__(node, engine)
        self.state["value"] = copy.deepcopy(self.config.get("value"))

    def action(self, name, args):
        if name == "click":
            return self.emit("trigger", {"value": self.config["payload"],
                                         "time": time.time()}).result(timeout=15)
        if name == "set_value":
            value = args["value"]
            kind = self.node["plugin"]
            if kind in ("control.number", "control.slider"):
                if not isinstance(value, (int, float)) or isinstance(value, bool):
                    raise WorkbenchError("需要数值")
                if not self.config["minimum"] <= value <= self.config["maximum"]:
                    raise WorkbenchError("数值超出控件范围")
            elif kind == "control.checkbox" and not isinstance(value, bool):
                raise WorkbenchError("需要布尔值")
            elif kind == "control.select" and value not in self.config["choices"]:
                raise WorkbenchError("选项不在列表中")
            elif kind == "control.text" and not isinstance(value, str):
                raise WorkbenchError("需要文本")
            with self.lock:
                self.state["value"] = value
            return self.emit("value", value).result(timeout=15)
        return super().action(name, args)


class Display(NodeRuntime):
    def receive(self, port, data):
        with self.lock:
            self.state["value"] = copy.deepcopy(data)


def control_view(runtime, request, properties):
    """Build the plugin's native control; initial assignment never emits a command."""
    from PySide6.QtCore import Qt, QTimer, QSignalBlocker
    from PySide6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
                                  QLineEdit, QDoubleSpinBox, QSlider, QCheckBox, QComboBox)
    widget = QWidget()
    layout = QVBoxLayout(widget)
    cfg = runtime.config
    label = QLabel(properties.get("label", cfg.get("label", runtime.node["name"])))
    label.setWordWrap(True)
    layout.addWidget(label)
    kind = runtime.node["plugin"]
    value = runtime.state.get("value")
    if kind == "control.button":
        editor = QPushButton(properties.get("text", cfg["label"]))
        editor.clicked.connect(lambda: request("click", {}))
    elif kind in ("control.number", "control.slider"):
        editor = QDoubleSpinBox()
        editor.setDecimals(cfg["decimals"])
        editor.setRange(cfg["minimum"], cfg["maximum"])
        editor.setSingleStep(cfg["step"])
        editor.setValue(value)
        editor.setSuffix((" " + cfg["unit"]) if cfg["unit"] else "")
        editor.setKeyboardTracking(False)
        editor.valueChanged.connect(lambda number: request("set_value", {"value": number}))
        if kind == "control.slider":
            slider = QSlider(Qt.Orientation.Horizontal)
            slider.setRange(0, 10000)
            span = cfg["maximum"] - cfg["minimum"]
            if span <= 0:
                raise WorkbenchError("滑块 maximum 必须大于 minimum")
            slider.setValue(round((value - cfg["minimum"]) / span * 10000))
            slider.setObjectName(f"{runtime.id}.slider")
            slider.valueChanged.connect(lambda number: editor.setValue(cfg["minimum"] + span * number / 10000))
            layout.addWidget(slider)
    elif kind == "control.checkbox":
        editor = QCheckBox(cfg["label"])
        editor.setChecked(bool(value))
        editor.toggled.connect(lambda checked: request("set_value", {"value": checked}))
    elif kind == "control.select":
        editor = QComboBox()
        editor.addItems([str(choice) for choice in cfg["choices"]])
        editor.setCurrentText(str(value))
        editor.currentTextChanged.connect(lambda text: request("set_value", {"value": text}))
    elif kind == "control.text":
        editor = QLineEdit(str(value))
        editor.setPlaceholderText(cfg["placeholder"])
        editor.returnPressed.connect(lambda: request("set_value", {"value": editor.text()}))
        apply = QPushButton("应用")
        apply.clicked.connect(lambda: request("set_value", {"value": editor.text()}))
        layout.addWidget(apply)
    else:
        editor = QLabel("—")
        editor.setWordWrap(True)
        editor.setProperty("role", "value")
        def refresh():
            current = runtime.snapshot().get("value")
            if current is not None:
                editor.setText(cfg["format"].format(value=current))
        timer = QTimer(widget)
        timer.timeout.connect(refresh)
        timer.start(100)
        refresh()
    editor.setObjectName(f"{runtime.id}.value")
    layout.addWidget(editor)
    layout.addStretch()
    if kind in ("control.number", "control.slider", "control.checkbox", "control.select", "control.text"):
        def refresh_control():
            current = runtime.snapshot().get("value")
            with QSignalBlocker(editor):
                if kind in ("control.number", "control.slider"):
                    editor.setValue(float(current))
                    if kind == "control.slider":
                        with QSignalBlocker(slider):
                            slider.setValue(round((float(current) - cfg["minimum"]) /
                                                  (cfg["maximum"] - cfg["minimum"]) * 10000))
                elif kind == "control.checkbox":
                    editor.setChecked(bool(current))
                elif kind == "control.select":
                    editor.setCurrentText(str(current))
                elif not editor.hasFocus():
                    editor.setText(str(current))
        timer = QTimer(widget)
        timer.timeout.connect(refresh_control)
        timer.start(100)
        widget.refresh_from_runtime = refresh_control
    elif kind == "display.value":
        widget.refresh_from_runtime = refresh
    return widget


class Debug(NodeRuntime):
    """Bounded receive history, editable shortcuts, periodic send and cancellable files."""

    def __init__(self, node, engine):
        super().__init__(node, engine)
        self.chunks = deque()
        self.retained = 0
        self.sequence = 0
        self.clear_sequence = 0
        self.history = list(self.config["history"])
        self.periodic_stop = threading.Event()
        self.file_cancel = threading.Event()
        self.periodic_thread = None
        self.state.update(rx_bytes=0, queued_tx_bytes=0, periodic=False, history=self.history,
                          file_progress=0, file_size=0)

    def receive(self, port, data):
        if port == "send":
            delivery = self.emit("tx", data)
            def count_delivered(future):
                if not future.cancelled() and future.exception() is None and future.result().get("delivered"):
                    with self.lock:
                        self.state["queued_tx_bytes"] += len(data)
            delivery.add_done_callback(count_delivered)
            return delivery
        with self.lock:
            self.sequence += 1
            self.chunks.append((self.sequence, time.time(), bytes(data)))
            self.retained += len(data)
            while self.retained > self.config["history_bytes"] and len(self.chunks) > 1:
                self.retained -= len(self.chunks.popleft()[2])
            self.state["rx_bytes"] += len(data)
        return self.emit("through", data)

    def read_since(self, cursor=0):
        """Read retained data separately from lightweight status polling."""
        with self.lock:
            return {"cursor": self.sequence,
                    "reset": cursor < self.clear_sequence or bool(
                        self.chunks and cursor < self.chunks[0][0] - 1),
                    "chunks": [(number, stamp, data) for number, stamp, data in self.chunks
                               if number > cursor]}

    def _send(self, args):
        text = str(args.get("text", ""))
        data = encode_text(text, args.get("encoding", self.config["encoding"]),
                           bool(args.get("hex", False)), bool(args.get("escape", True)),
                           args.get("newline", self.config["newline"]))
        receipt = self.emit("tx", data).result(timeout=15)
        if not receipt.get("delivered"):
            raise WorkbenchError("发送输出未连接，数据未发送")
        with self.lock:
            self.state["queued_tx_bytes"] += len(data)
            if text and (not self.history or self.history[-1] != text):
                self.history.append(text)
                del self.history[:-100]
                self.state["history"] = list(self.history)
        return {"bytes": len(data), "delivery": receipt}

    def action(self, name, args):
        if name == "send":
            return self._send(args)
        if name == "read":
            result = self.read_since(int(args.get("cursor", 0)))
            limit = min(1048576, max(1, int(args.get("limit", 65536))))
            selected, total = [], 0
            for number, stamp, data in result["chunks"]:
                if total + len(data) > limit and selected:
                    break
                selected.append({"sequence": number, "time": stamp, "data": data})
                total += len(data)
            return {"chunks": selected, "cursor": selected[-1]["sequence"] if selected else args.get("cursor", 0),
                    "reset": result["reset"]}
        if name == "clear":
            with self.lock:
                self.chunks.clear()
                self.retained = 0
                self.sequence += 1
                self.clear_sequence = self.sequence
                self.state.update(rx_bytes=0, queued_tx_bytes=0)
            return self.snapshot()
        if name == "save_receive":
            path = Path(args["path"])
            with self.lock:
                chunks = list(self.chunks)
            if args.get("binary", True):
                path.write_bytes(b"".join(data for _, _, data in chunks))
            else:
                path.write_text("".join(data.decode(self.config["encoding"], "replace")
                                        for _, _, data in chunks), encoding="utf-8")
            return {"path": str(path.resolve()), "bytes": path.stat().st_size}
        if name == "send_file":
            path = Path(args["path"])
            self.file_cancel.clear()
            size = path.stat().st_size
            with self.lock:
                self.state.update(file_progress=0, file_size=size)
            with path.open("rb") as stream:
                while not self.file_cancel.is_set() and self.engine.running:
                    data = stream.read(16384)
                    if not data:
                        return {"complete": True, "bytes": size}
                    delivery = self.emit("tx", data).result(timeout=30)
                    if not delivery.get("delivered"):
                        raise WorkbenchError("文件发送输出未连接，数据未发送")
                    with self.lock:
                        self.state["file_progress"] += len(data)
            return {"complete": False, "cancelled": True,
                    "bytes": self.state["file_progress"]}
        if name == "cancel_file":
            self.file_cancel.set()
            return {"cancel_requested": True}
        if name == "periodic_stop":
            self.periodic_stop.set()
            with self.lock:
                self.state["periodic"] = False
            return self.snapshot()
        if name == "periodic_start":
            if self.periodic_thread and self.periodic_thread.is_alive():
                raise WorkbenchError("定时发送已运行")
            interval = float(args.get("interval_ms", 1000)) / 1000
            if interval < 0.001:
                raise WorkbenchError("定时间隔不能小于 1 ms")
            self.periodic_stop.clear()
            payload = copy.deepcopy(args)
            def repeat():
                with self.lock:
                    self.state["periodic"] = True
                while self.engine.running and not self.periodic_stop.is_set():
                    try:
                        self._send(payload)
                    except Exception as exc:
                        self.engine.record("error", f"定时发送：{exc}")
                        break
                    if self.periodic_stop.wait(interval):
                        break
                with self.lock:
                    self.state["periodic"] = False
            self.periodic_thread = threading.Thread(target=repeat, daemon=True,
                                                     name=f"periodic-{self.id}")
            self.periodic_thread.start()
            return {"started": True}
        return super().action(name, args)

    def stop(self):
        self.periodic_stop.set()
        self.file_cancel.set()
        super().stop()
        if self.periodic_thread:
            self.periodic_thread.join(timeout=1)


def debug_view(runtime, request, properties):
    """The native send/receive panel retains keyboard, history, file and timer flows."""
    from datetime import datetime
    from PySide6.QtCore import Qt, QTimer
    from PySide6.QtGui import QKeySequence, QShortcut, QFont
    from PySide6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QPlainTextEdit,
                                  QCheckBox, QComboBox, QPushButton, QSpinBox,
                                  QFileDialog, QLabel)
    widget = QWidget()
    layout = QVBoxLayout(widget)
    toolbar = QHBoxLayout()
    receive_hex = QCheckBox("HEX")
    timestamp = QCheckBox("时间戳")
    timestamp.setChecked(True)
    follow = QCheckBox("跟随")
    follow.setChecked(True)
    clear = QPushButton("清空")
    save = QPushButton("保存接收")
    for item in (receive_hex, timestamp, follow, clear, save):
        toolbar.addWidget(item)
    toolbar.addStretch()
    layout.addLayout(toolbar)
    receive = QPlainTextEdit()
    receive.setReadOnly(True)
    receive.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
    receive.setMaximumBlockCount(10000)
    receive.setFont(QFont("Consolas", 10))
    receive.setObjectName(f"{runtime.id}.receive")
    layout.addWidget(receive, 4)
    history = QComboBox()
    history.setEditable(True)
    history.addItems(runtime.history)
    history.setCurrentText("")
    history.setObjectName(f"{runtime.id}.send_text")
    layout.addWidget(history)
    options = QHBoxLayout()
    send_hex = QCheckBox("HEX 发送")
    escape = QCheckBox("转义")
    escape.setChecked(True)
    newline = QComboBox()
    newline.addItems(["None", "LF", "CRLF"])
    newline.setCurrentText(runtime.config["newline"])
    encoding = QComboBox()
    encoding.setEditable(True)
    encoding.addItems(["utf-8", "gbk", "ascii", "utf-16"])
    encoding.setCurrentText(runtime.config["encoding"])
    send = QPushButton("发送")
    send.setObjectName(f"{runtime.id}.send")
    for item in (send_hex, escape, newline, encoding, send):
        options.addWidget(item)
    layout.addLayout(options)
    files = QHBoxLayout()
    send_file = QPushButton("发送文件")
    cancel_file = QPushButton("取消发送")
    periodic = QCheckBox("定时发送")
    interval = QSpinBox()
    interval.setRange(1, 3600000)
    interval.setValue(1000)
    interval.setSuffix(" ms")
    for item in (send_file, cancel_file, periodic, interval):
        files.addWidget(item)
    layout.addLayout(files)
    quick = QHBoxLayout()
    for index, command in enumerate(runtime.config["quick_commands"]):
        button = QPushButton(command.get("label", f"指令 {index + 1}"))
        button.setObjectName(f"{runtime.id}.quick.{index}")
        button.clicked.connect(lambda _, entry=command: request("send", entry))
        if command.get("shortcut"):
            shortcut = QShortcut(QKeySequence(command["shortcut"]), widget)
            shortcut.activated.connect(lambda entry=command: request("send", entry))
        quick.addWidget(button)
    layout.addLayout(quick)
    status = QLabel("接收 0 B")
    layout.addWidget(status)
    cursor = [0]
    decoder = [codecs.getincrementaldecoder(encoding.currentText())(errors="replace")]
    def payload():
        return {"text": history.currentText(), "hex": send_hex.isChecked(),
                "escape": escape.isChecked(), "newline": newline.currentText(),
                "encoding": encoding.currentText()}
    def transmit():
        text = history.currentText()
        request("send", payload())
        if text and history.findText(text) < 0:
            history.addItem(text)
    send.clicked.connect(transmit)
    shortcut = QShortcut(QKeySequence("Ctrl+Return"), widget)
    shortcut.activated.connect(transmit)
    def clear_display():
        request("clear", {})
        receive.clear()
        cursor[0] = runtime.sequence
        decoder[0].reset()
    clear.clicked.connect(clear_display)
    def receive_mode_changed():
        try:
            candidate = codecs.getincrementaldecoder(encoding.currentText())(errors="replace")
        except LookupError:
            return
        receive.clear()
        cursor[0] = 0
        decoder[0] = candidate
    receive_hex.toggled.connect(receive_mode_changed)
    encoding.currentTextChanged.connect(receive_mode_changed)
    def refresh():
        result = runtime.read_since(cursor[0])
        if result["reset"]:
            receive.clear()
            decoder[0].reset()
        if result["chunks"]:
            scrollbar = receive.verticalScrollBar()
            previous = scrollbar.value()
            text = ""
            for _, stamp, data in result["chunks"]:
                if timestamp.isChecked():
                    text += datetime.fromtimestamp(stamp).strftime("[%H:%M:%S.%f] ")[:-4] + " "
                text += (data.hex(" ") + "\n") if receive_hex.isChecked() else decoder[0].decode(data)
            receive.moveCursor(receive.textCursor().MoveOperation.End)
            receive.insertPlainText(text)
            if follow.isChecked():
                scrollbar.setValue(scrollbar.maximum())
            else:
                scrollbar.setValue(previous)
        cursor[0] = result["cursor"]
        state = runtime.snapshot()
        status.setText(f"接收 {state['rx_bytes']:,} B　已交付发送 {state['queued_tx_bytes']:,} B"
                       + (f"　文件 {state['file_progress']:,}/{state['file_size']:,} B" if state["file_size"] else ""))
    timer = QTimer(widget)
    timer.timeout.connect(refresh)
    timer.start(50)
    def choose_file():
        path, _ = QFileDialog.getOpenFileName(widget, "发送文件", options=QFileDialog.Option.DontUseNativeDialog)
        if path:
            request("send_file", {"path": path})
    def save_receive():
        path, _ = QFileDialog.getSaveFileName(widget, "保存接收", options=QFileDialog.Option.DontUseNativeDialog)
        if path:
            request("save_receive", {"path": path, "binary": True})
    send_file.clicked.connect(choose_file)
    cancel_file.clicked.connect(lambda: request("cancel_file", {}))
    save.clicked.connect(save_receive)
    periodic.toggled.connect(lambda checked: request(
        "periodic_start" if checked else "periodic_stop",
        {**payload(), "interval_ms": interval.value()}))
    widget.refresh_from_runtime = refresh
    return widget


class Recorder(NodeRuntime):
    """A recording node can be inserted inline without requiring a split wire."""

    def __init__(self, node, engine):
        super().__init__(node, engine)
        self.stream = None
        self.state["written_bytes"] = 0

    def receive(self, port, data):
        if self.stream is None:
            path = Path(self.config["path"])
            if not path.is_absolute():
                path = Path(self.engine.base_dir) / path
            path.parent.mkdir(parents=True, exist_ok=True)
            self.stream = path.open("ab" if self.config["append"] else "wb")
        output = data if self.config["format"] == "binary" else (
            f"{time.time():.6f} {data.hex(' ')}\n".encode("ascii"))
        self.stream.write(output)
        self.stream.flush()
        with self.lock:
            self.state["written_bytes"] += len(output)
        return self.emit("through", data)

    def stop(self):
        super().stop()
        if self.stream is not None:
            self.stream.close()
            self.stream = None


class Timer(NodeRuntime):
    def __init__(self, node, engine):
        super().__init__(node, engine)
        self.thread = None
        self.timer_stop = threading.Event()
        self.state["ticks"] = 0

    def action(self, name, args):
        if name == "stop":
            self.timer_stop.set()
            return {"stopping": True}
        if name == "start":
            if self.thread and self.thread.is_alive():
                raise WorkbenchError("定时器已运行")
            self.timer_stop.clear()
            interval = float(args.get("interval_ms", self.config["interval_ms"])) / 1000
            if interval < 0.001:
                raise WorkbenchError("定时间隔不能小于 1 ms")
            def run():
                deadline = time.monotonic()
                while self.engine.running and not self.timer_stop.is_set():
                    with self.lock:
                        self.state["ticks"] += 1
                        tick = self.state["ticks"]
                    try:
                        self.emit("tick", {"value": tick, "time": time.time()})
                    except Exception as exc:
                        self.engine.record("error", str(exc))
                        break
                    deadline += interval
                    if self.timer_stop.wait(max(0, deadline - time.monotonic())):
                        break
            self.thread = threading.Thread(target=run, daemon=True, name=f"timer-{self.id}")
            self.thread.start()
            return {"started": True}
        return super().action(name, args)

    def stop(self):
        self.timer_stop.set()
        super().stop()
        if self.thread:
            self.thread.join(timeout=1)


def timer_view(runtime, request, properties):
    from PySide6.QtWidgets import QWidget, QHBoxLayout, QPushButton, QSpinBox
    widget = QWidget()
    layout = QHBoxLayout(widget)
    interval = QSpinBox()
    interval.setRange(1, 3600000)
    interval.setValue(runtime.config["interval_ms"])
    interval.setSuffix(" ms")
    start, stop = QPushButton("启动定时器"), QPushButton("停止")
    start.clicked.connect(lambda: request("start", {"interval_ms": interval.value()}))
    stop.clicked.connect(lambda: request("stop", {}))
    for item in (interval, start, stop):
        layout.addWidget(item)
    return widget


def register(registry):
    numeric = {"label": {"type": "string", "default": "数值"},
               "value": {"type": "number", "default": 0.0},
               "minimum": {"type": "number", "default": -100.0},
               "maximum": {"type": "number", "default": 100.0},
               "step": {"type": "number", "default": 0.1, "min": 0.000001},
               "decimals": {"type": "integer", "default": 3, "min": 0, "max": 12},
               "unit": {"type": "string", "default": ""}}
    for ident, name in (("control.number", "数值输入"), ("control.slider", "滑块")):
        registry.register(Definition(ident, name, "控件", "可编辑数值，按范围校验后发布数值事件",
            (Port("value", "数值", "out", "number"),), numeric, Control,
            {"control": control_view}, {"set_value": {"description": "设置并发布 value"}}))
    for ident, name, kind, defaults in (
        ("control.text", "文本输入", "text", {"label": {"type": "string", "default": "文本"},
            "value": {"type": "string", "default": ""}, "placeholder": {"type": "string", "default": "输入后按 Enter 应用"}}),
        ("control.checkbox", "开关", "bool", {"label": {"type": "string", "default": "开关"},
            "value": {"type": "boolean", "default": False}}),
        ("control.select", "下拉选项", "text", {"label": {"type": "string", "default": "选项"},
            "choices": {"type": "json", "default": ["A", "B"]}, "value": {"type": "string", "default": "A"}}),
    ):
        registry.register(Definition(ident, name, "控件", "用户与 API 共用同一控件操作",
            (Port("value", "值", "out", kind),), defaults, Control, {"control": control_view},
            {"set_value": {"description": "设置并发布 value"}}))
    registry.register(Definition("control.button", "按钮", "控件", "按下或通过 API 触发事件",
        (Port("trigger", "触发", "out", "event"),),
        {"label": {"type": "string", "default": "执行"},
         "payload": {"type": "json", "default": "hello"}},
        Control, {"control": control_view}, {"click": {"description": "触发按钮事件"}}))
    registry.register(Definition("display.value", "数值 / 文本显示", "控件", "显示输入的最新值",
        (Port("value", "值", "in", "any"),),
        {"label": {"type": "string", "default": "当前值"},
         "format": {"type": "string", "default": "{value}"}}, Display, {"control": control_view}))
    registry.register(Definition("debug.console", "收发调试", "调试",
        "文本/HEX、编码、时间戳、历史、快捷指令、定时发送和文件收发",
        (Port("rx", "接收", "in", "bytes"), Port("send", "发送请求", "in", "bytes"),
         Port("through", "原样继续", "out", "bytes"),
         Port("tx", "发送", "out", "bytes")),
        {"encoding": {"type": "string", "default": "utf-8"},
         "newline": {"type": "string", "default": "None", "choices": ["None", "LF", "CRLF"]},
         "history_bytes": {"type": "integer", "default": 1048576, "min": 1024, "max": 67108864},
         "history": {"type": "json", "default": []},
         "quick_commands": {"type": "json", "default": []}}, Debug, {"console": debug_view},
        {"send": {"description": "发送 text/encoding/hex/escape/newline"},
         "read": {"description": "读取 cursor 之后的保留数据"},
         "clear": {"description": "清空收发显示和计数", "immediate": True},
         "save_receive": {"description": "将保留数据保存为真实文件"},
         "send_file": {"description": "分块发送文件并等待下游完成"},
         "cancel_file": {"description": "取消文件发送", "immediate": True},
         "periodic_start": {"description": "启动定时发送"},
         "periodic_stop": {"description": "停止定时发送", "immediate": True}}))
    registry.register(Definition("data.recorder", "数据记录", "调试", "记录原始字节或带时间戳的 HEX；原样继续传递",
        (Port("bytes", "输入字节", "in", "bytes"), Port("through", "原样继续", "out", "bytes")),
        {"path": {"type": "string", "default": "records/receive.bin"},
         "append": {"type": "boolean", "default": True},
         "format": {"type": "string", "default": "binary", "choices": ["binary", "hex"]}}, Recorder))
    registry.register(Definition("control.timer", "定时触发", "控件", "显式启停的周期事件源，打开工程不会自动启动",
        (Port("tick", "触发", "out", "event"),),
        {"interval_ms": {"type": "integer", "default": 1000, "min": 1, "max": 3600000}},
        Timer, {"timer": timer_view},
        {"start": {"description": "启动定时器"}, "stop": {"description": "停止定时器", "immediate": True}}))
