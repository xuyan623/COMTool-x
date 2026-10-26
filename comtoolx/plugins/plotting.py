"""Native pyqtgraph plots; acquisition history is independent of repaint cadence."""

from __future__ import annotations

import csv
import time
from pathlib import Path

import numpy as np

from ..sdk import Definition, NodeRuntime, Port, WorkbenchError

COLORS = ["#38BDF8", "#FB923C", "#4ADE80", "#F472B6", "#A78BFA", "#FACC15",
          "#2DD4BF", "#F87171", "#818CF8", "#E879F9", "#A3E635", "#FDA4AF",
          "#22D3EE", "#C084FC", "#FDBA74", "#E2E8F0"]


class SeriesBuffer:
    """A bounded numeric ring with allocation-free appends and windowed snapshots."""

    def __init__(self, capacity):
        self.capacity = capacity
        self.x = np.empty(capacity, dtype=np.float64)
        self.y = np.empty(capacity, dtype=np.float64)
        self.count = 0
        self.end = 0
        self.total = 0
        self.monotonic = True
        self.last_x = None

    def append(self, x, y):
        x, y = np.asarray(x, dtype=np.float64), np.asarray(y, dtype=np.float64)
        if x.ndim != 1 or y.ndim != 1 or len(x) != len(y):
            raise WorkbenchError("曲线 x/y 需要等长的一维序列")
        size = len(x)
        if not size:
            return
        self.monotonic = (self.monotonic and (self.last_x is None or x[0] >= self.last_x)
                          and bool(np.all(x[1:] >= x[:-1])))
        self.last_x = float(x[-1])
        self.total += size
        if size >= self.capacity:
            self.x[:], self.y[:] = x[-self.capacity:], y[-self.capacity:]
            self.count, self.end = self.capacity, 0
            return
        first = min(size, self.capacity - self.end)
        self.x[self.end:self.end + first], self.y[self.end:self.end + first] = x[:first], y[:first]
        if first < size:
            self.x[:size - first], self.y[:size - first] = x[first:], y[first:]
        self.end = (self.end + size) % self.capacity
        self.count = min(self.capacity, self.count + size)

    def arrays(self, window=None):
        """Copy only the visible monotonic time window when following live data."""
        start = (self.end - self.count) % self.capacity
        segments = [(start, start + self.count)] if start + self.count <= self.capacity else [
            (start, self.capacity), (0, self.end)]
        cutoff = self.last_x - window if window and self.monotonic and self.last_x is not None else None
        xx, yy = [], []
        for begin, end in segments:
            x, y = self.x[begin:end], self.y[begin:end]
            if cutoff is not None and len(x):
                offset = max(0, int(np.searchsorted(x, cutoff, side="left")) - 1)
                x, y = x[offset:], y[offset:]
            if len(x):
                xx.append(x)
                yy.append(y)
        if not xx:
            return np.empty(0), np.empty(0)
        return np.concatenate(xx), np.concatenate(yy)


class Plot(NodeRuntime):
    def __init__(self, node, engine):
        super().__init__(node, engine)
        self.series = {}
        self.version = 0
        self.state.update(points=0, paused=False, follow=self.config["follow"],
                          window=self.config["window_seconds"],
                          colors=self.config["colors"].copy(), visibility=self.config["visibility"].copy())
        for index, channel in enumerate(self.config["channels"]):
            self.state["colors"].setdefault(channel, COLORS[index % len(COLORS)])
            self.state["visibility"].setdefault(channel, True)

    def receive(self, port, data):
        """Accept decoded batches; the GUI never runs the byte parser."""
        if not isinstance(data, dict):
            raise WorkbenchError("曲线输入需要 {通道: {x: [...], y: [...]}}")
        with self.lock:
            for name, values in data.items():
                if name not in self.series:
                    self.series[name] = SeriesBuffer(self.config["max_points"])
                buffer = self.series[name]
                buffer.append(values["x"], values["y"])
                self.state["points"] += len(values["x"])
                self.state["colors"].setdefault(name, COLORS[(len(self.series) - 1) % len(COLORS)])
                self.state["visibility"].setdefault(name, True)
            self.version += 1

    def plot_data(self):
        with self.lock:
            window = self.state["window"] if self.state["follow"] else None
            data = {
                name: {"x": xy[0], "y": xy[1], "total": buffer.total, "retained": buffer.count}
                for name, buffer in self.series.items() for xy in [buffer.arrays(window)]
            }
            for channel in self.config["channels"]:
                data.setdefault(channel, {"x": np.empty(0), "y": np.empty(0), "total": 0, "retained": 0})
            return self.version, data, self.snapshot()

    def action(self, name, args):
        if name == "export_csv":
            path = Path(args["path"]).resolve()
            with self.lock:
                data = {channel: buffer.arrays() for channel, buffer in self.series.items()}
            # File formatting follows the snapshot, outside the acquisition lock.
            with path.open("w", encoding="utf-8-sig", newline="") as stream:
                writer = csv.writer(stream)
                writer.writerow(["channel", "x", "y"])
                for channel, (xx, yy) in data.items():
                    writer.writerows((channel, float(x), float(y)) for x, y in zip(xx, yy))
            return {"path": str(path), "channels": len(data)}
        with self.lock:
            if name == "clear":
                self.series.clear()
                self.state["points"] = 0
                self.version += 1
                return self.snapshot()
            if name in ("pause", "follow"):
                key = "paused" if name == "pause" else "follow"
                self.state[key] = bool(args["value"])
                self.version += 1
                return self.snapshot()
            if name == "set_window":
                seconds = float(args["seconds"])
                if not 0 < seconds <= 86400:
                    raise WorkbenchError("时间窗需要在 0～86400 秒之间")
                self.state["window"] = seconds
                self.version += 1
                return self.snapshot()
            if name in ("set_color", "set_visible"):
                channel = args["channel"]
                if channel not in self.series and channel not in self.config["channels"] and channel not in self.state["colors"]:
                    raise WorkbenchError("曲线通道尚不存在")
                key = "colors" if name == "set_color" else "visibility"
                self.state[key][channel] = args["color"] if name == "set_color" else bool(args["visible"])
                self.version += 1
                return self.snapshot()
            if name == "data":
                limit = min(1000000, max(1, int(args.get("limit", 1000))))
                names = [args["channel"]] if "channel" in args else list(self.series)
                result = {}
                for channel in names:
                    if channel not in self.series:
                        raise WorkbenchError(f"通道不存在：{channel}")
                    buffer = self.series[channel]
                    x, y = buffer.arrays()
                    result[channel] = {"x": x[-limit:].tolist(), "y": y[-limit:].tolist(),
                                       "total": buffer.total, "retained": buffer.count}
                return result
        return super().action(name, args)

    def snapshot(self):
        with self.lock:
            return {**super().snapshot(), "version": self.version,
                    "channels": {name: {"retained": series.count, "total": series.total,
                                         "last_x": series.last_x,
                                         "last_y": float(series.y[(series.end - 1) % series.capacity]) if series.count else None}
                                 for name, series in self.series.items()}}


def plot_view(runtime, request, properties):
    """Keep pyqtgraph's zoom/pan, context menu, FFT and native exporters intact."""
    import pyqtgraph as pg
    import pyqtgraph.exporters
    from PySide6.QtCore import Qt, QTimer, QSignalBlocker
    from PySide6.QtGui import QColor
    from PySide6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QSplitter,
                                  QPushButton, QCheckBox, QDoubleSpinBox, QTableWidget,
                                  QTableWidgetItem, QColorDialog, QFileDialog, QLabel,
                                  QToolButton, QMenu, QHeaderView)
    widget = QWidget()
    widget.setMinimumSize(300, 210)
    layout = QVBoxLayout(widget)
    layout.setContentsMargins(0, 0, 0, 0)
    bar = QHBoxLayout()
    pause, clear, auto_y = QPushButton("暂停波形"), QPushButton("清空"), QPushButton("自适应 Y")
    follow = QCheckBox("跟随最新")
    follow.setChecked(runtime.state["follow"])
    window = QDoubleSpinBox()
    window.setRange(0.01, 86400)
    window.setValue(runtime.state["window"])
    window.setSuffix(" s")
    window.setDecimals(2)
    export = QToolButton()
    export.setText("导出")
    export.setPopupMode(QToolButton.ToolButtonPopupMode.InstantPopup)
    menu = QMenu(export)
    export.setMenu(menu)
    for name, child in (("pause", pause), ("clear", clear), ("auto_y", auto_y),
                        ("follow", follow), ("window", window), ("export", export)):
        child.setObjectName(f"{runtime.id}.plot.{name}")
        bar.addWidget(child)
    bar.addStretch()
    layout.addLayout(bar)
    split = QSplitter(Qt.Orientation.Horizontal)
    graph = pg.GraphicsLayoutWidget()
    graph.setObjectName(f"{runtime.id}.plot.canvas")
    graph.setBackground("#101724" if properties.get("theme", "dark") == "dark" else "#FFFFFF")
    plot = graph.addPlot()
    plot.showGrid(x=True, y=True, alpha=0.15)
    plot.setLabel("bottom", runtime.config["x_label"], units=runtime.config["x_unit"])
    plot.setLabel("left", runtime.config["y_label"], units=runtime.config["y_unit"])
    plot.setXRange(0, runtime.config["window_seconds"], padding=0)
    plot.setMenuEnabled(True)
    table = QTableWidget(0, 3)
    table.setHorizontalHeaderLabels(["通道", "当前值", "颜色"])
    table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
    table.setMinimumWidth(210)
    table.setMaximumWidth(420)
    table.verticalHeader().hide()
    table.setColumnWidth(1, 74)
    table.setColumnWidth(2, 42)
    table.setObjectName(f"{runtime.id}.plot.channels")
    split.addWidget(graph)
    split.addWidget(table)
    split.setStretchFactor(0, 5)
    split.setStretchFactor(1, 1)
    split.setSizes([800, 290])
    layout.addWidget(split, 1)
    status = QLabel("等待数据")
    status.setProperty("role", "muted")
    layout.addWidget(status)
    curves = {}
    last_version = [-1]
    last_draw = [time.monotonic()]
    frames = [0]
    pause.clicked.connect(lambda: request("pause", {"value": not runtime.snapshot()["paused"]}))
    clear.clicked.connect(lambda: request("clear", {}))
    follow.toggled.connect(lambda value: request("follow", {"value": value}))
    window.valueChanged.connect(lambda value: request("set_window", {"seconds": value}))
    auto_y.clicked.connect(lambda: plot.enableAutoRange(axis="y", enable=True))
    plot.getViewBox().sigRangeChangedManually.connect(lambda _: request("follow", {"value": False}))
    def channel_changed(item):
        if item.column() == 0:
            request("set_visible", {"channel": item.text(),
                                   "visible": item.checkState() == Qt.CheckState.Checked})
    table.itemChanged.connect(channel_changed)
    def choose_color(row, column):
        if column != 2:
            return
        name = table.item(row, 0).text()
        color = QColorDialog.getColor(QColor(runtime.snapshot()["colors"].get(name)), widget,
                                      f"{name} 曲线颜色")
        if color.isValid():
            request("set_color", {"channel": name, "color": color.name()})
    table.cellDoubleClicked.connect(choose_color)
    def refresh():
        state = runtime.snapshot()
        pause.setText("恢复波形" if state["paused"] else "暂停波形")
        with QSignalBlocker(follow):
            follow.setChecked(state["follow"])
        with QSignalBlocker(window):
            window.setValue(state["window"])
        if state["paused"] or state["version"] == last_version[0]:
            return
        version, data, state = runtime.plot_data()
        last_version[0] = version
        if not data:
            plot.clear()
            curves.clear()
            table.setRowCount(0)
            status.setText("等待数据")
            return
        blocker = QSignalBlocker(table)
        table.setRowCount(len(data))
        newest = None
        for row, (name, values) in enumerate(data.items()):
            color = state["colors"][name]
            if name not in curves:
                curves[name] = plot.plot(name=name, pen=pg.mkPen(color, width=runtime.config["line_width"]))
                curves[name].setDownsampling(auto=True, method="peak")
                curves[name].setClipToView(True)
            curve = curves[name]
            curve.setPen(pg.mkPen(color, width=runtime.config["line_width"]))
            curve.setVisible(state["visibility"].get(name, True))
            curve.setData(values["x"], values["y"])
            if len(values["x"]):
                newest = max(newest, float(values["x"][-1])) if newest is not None else float(values["x"][-1])
            item = table.item(row, 0) or QTableWidgetItem(name)
            item.setFlags(Qt.ItemFlag.ItemIsEnabled | Qt.ItemFlag.ItemIsSelectable | Qt.ItemFlag.ItemIsUserCheckable)
            item.setCheckState(Qt.CheckState.Checked if state["visibility"].get(name, True) else Qt.CheckState.Unchecked)
            table.setItem(row, 0, item)
            item.setToolTip(name)
            value = f"{values['y'][-1]:.6g}" if len(values["y"]) else "—"
            value_item = QTableWidgetItem(value)
            value_item.setFlags(Qt.ItemFlag.ItemIsEnabled | Qt.ItemFlag.ItemIsSelectable)
            table.setItem(row, 1, value_item)
            color_item = QTableWidgetItem("■")
            color_item.setForeground(QColor(color))
            color_item.setFlags(Qt.ItemFlag.ItemIsEnabled | Qt.ItemFlag.ItemIsSelectable)
            table.setItem(row, 2, color_item)
        del blocker
        if state["follow"] and newest is not None:
            plot.setXRange(newest - state["window"], newest, padding=0)
        frames[0] += 1
        elapsed = time.monotonic() - last_draw[0]
        retained = sum(channel["retained"] for channel in state["channels"].values())
        status.setText(f"{len(data)} 路通道　累计 {state['points']:,} 点　保留 {retained:,} 点"
                       + ("　等待数据" if state["points"] == 0 else "")
                       + (f"　绘制 {frames[0] / elapsed:.0f} 次/s" if elapsed > 0.5 else ""))
        if elapsed > 2:
            frames[0], last_draw[0] = 0, time.monotonic()
    timer = QTimer(widget)
    timer.timeout.connect(refresh)
    timer.start(runtime.config["refresh_ms"])
    def export_image(args):
        path = Path(args["path"]).resolve()
        if path.suffix.lower() == ".svg":
            # SVG COMPAT BEGIN: Qt 6.12 emits space-separated path coordinates,
            # while pyqtgraph 0.14's correctCoordinates assumes comma pairs.
            # Render with Qt's supported SVG paint device, without modifying a
            # dependency. Remove when pyqtgraph's parser supports that Qt output.
            from PySide6.QtSvg import QSvgGenerator
            from PySide6.QtCore import QSize, QRectF
            from PySide6.QtGui import QPainter
            bounds = plot.sceneBoundingRect()
            generator = QSvgGenerator()
            generator.setFileName(str(path))
            generator.setSize(QSize(max(1, int(bounds.width())), max(1, int(bounds.height()))))
            generator.setViewBox(QRectF(0, 0, bounds.width(), bounds.height()))
            generator.setTitle(runtime.node["name"])
            painter = QPainter(generator)
            target = QRectF(0, 0, bounds.width(), bounds.height())
            painter.fillRect(target, QColor("#101724" if properties.get("theme", "dark") == "dark" else "#FFFFFF"))
            plot.scene().render(painter, target, bounds)
            painter.end()
            # SVG COMPAT END
        else:
            exporter = pg.exporters.ImageExporter(plot)
            if "width" in args:
                exporter.parameters()["width"] = int(args["width"])
            exporter.export(str(path))
        return {"path": str(path), "bytes": path.stat().st_size}
    def choose_export(kind):
        path, _ = QFileDialog.getSaveFileName(widget, "导出曲线", f"plot.{kind}",
            f"{kind.upper()} (*.{kind})", options=QFileDialog.Option.DontUseNativeDialog)
        if path:
            if kind == "csv":
                request("export_csv", {"path": path})
            else:
                export_image({"path": path})
    for kind in ("csv", "png", "svg"):
        menu.addAction(kind.upper(), lambda checked=False, fmt=kind: choose_export(fmt))
    widget.api_actions = {
        "export_image": export_image,
        "auto_y": lambda args: plot.enableAutoRange(axis="y", enable=True),
        "set_range": lambda args: plot.setRange(xRange=args.get("x"), yRange=args.get("y")),
        "refresh": lambda args: refresh(),
    }
    widget.plot_item = plot
    widget.graph_widget = graph
    widget.refresh_from_runtime = refresh
    return widget


def register(registry):
    registry.register(Definition("plot.curves", "实时曲线", "绘图",
        "原生 pyqtgraph：多通道、缩放平移、通道列表、显隐颜色、暂停跟随、FFT、CSV/PNG/SVG",
        (Port("samples", "曲线数据", "in", "samples"),),
        {"window_seconds": {"type": "number", "default": 10.0, "min": 0.01, "max": 86400},
         "follow": {"type": "boolean", "default": True},
         "refresh_ms": {"type": "integer", "default": 16, "min": 1, "max": 1000},
         "max_points": {"type": "integer", "default": 200000, "min": 100, "max": 10000000},
         "line_width": {"type": "number", "default": 1.0, "min": 0.5, "max": 5},
         "colors": {"type": "json", "default": {}},
         "channels": {"type": "json", "default": []},
         "visibility": {"type": "json", "default": {}},
         "x_label": {"type": "string", "default": "时间"},
         "x_unit": {"type": "string", "default": "s"},
         "y_label": {"type": "string", "default": "数值"},
         "y_unit": {"type": "string", "default": ""}},
        Plot, {"plot": plot_view},
        {name: {"description": description, "immediate": name not in ("export_csv",),
                "requires_running": False}
         for name, description in {
             "clear": "清空曲线与保留数据", "pause": "设置 value 暂停/恢复显示",
             "follow": "设置 value 跟随最新数据", "set_window": "设置 seconds 时间窗",
             "set_color": "设置 channel/color", "set_visible": "设置 channel/visible",
             "data": "读取原始保留点，支持 channel/limit", "export_csv": "导出保留数据到 path"
         }.items()}))
