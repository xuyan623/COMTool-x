"""Compare actual legacy/new native plot pipelines under the same bounded workload.

This is an offline measurement harness. It never connects hardware or changes
production refresh settings. Qt offscreen painting is not display refresh rate.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import platform
import statistics
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--legacy", action="store_true")
    parser.add_argument("--channels", type=int, default=8)
    parser.add_argument("--seconds", type=float, default=4)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--style", choices=["matched", "defaults"], default="matched")
    options = parser.parse_args()
    os.environ["PYQTGRAPH_QT_LIB"] = "PyQt5" if options.legacy else "PySide6"
    if options.legacy:
        from PyQt5.QtWidgets import QApplication
        from PyQt5.QtCore import QObject, QEvent, QTimer
    else:
        from PySide6.QtWidgets import QApplication
        from PySide6.QtCore import QObject, QEvent, QTimer
    import pyqtgraph as pg
    application = QApplication([])
    antialias = options.legacy or options.style == "matched"
    line_width = 2 if options.legacy or options.style == "matched" else 1
    pg.setConfigOptions(antialias=antialias)
    controller = None
    if options.legacy:
        from COMTool.plugins.graph_widgets import Graph_Plot
        widget = Graph_Plot(config={})
        graph = widget.plotWin
        ingest = widget.onData
        retained = lambda: sum(len(values["x"]) for values in widget.data.values())
    else:
        from comtoolx.sdk import Registry
        from comtoolx.plugins import register_builtins
        from comtoolx.controller import Controller
        from comtoolx.plugins.plotting import plot_view
        registry = Registry()
        register_builtins(registry)
        controller = Controller(registry, ROOT / "verification/benchmark-session")
        controller.command("graph.node.add", {"id": "decode", "plugin": "protocol.plot"})
        controller.command("graph.node.add", {"id": "plot", "plugin": "plot.curves",
            "config": {"max_points": 100000, "window_seconds": 10.0,
                       "line_width": float(line_width), "refresh_ms": 16}})
        controller.command("graph.wire.add", {
            "source": {"node": "decode", "port": "samples"},
            "target": {"node": "plot", "port": "samples"}})
        runtime = controller.engine.nodes["plot"]
        widget = plot_view(runtime, lambda name, args: controller.command(
            "node.action", {"id": "plot", "action": name, "args": args}), {})
        graph = widget.graph_widget
        controller.command("runtime.start")
        ingest = lambda frame: controller.engine.nodes["decode"].receive("bytes", frame)
        retained = lambda: runtime.snapshot()["points"]
    graph.setFixedSize(1200, 650)
    widget.resize(1540, 780)
    widget.show()

    def frames(begin, count):
        return "".join(f"$ch{channel},{sample / 1000:.3f},{math.sin(sample / 50 + channel):.7f}\n"
                       for sample in range(begin, begin + count)
                       for channel in range(options.channels)).encode()

    receipt = ingest(frames(0, 10000))
    if receipt is not None:
        receipt.result(timeout=15)
    application.processEvents()
    start, cpu_start = [None], [None]
    paints, gaps, errors = [0], [], []

    class PaintCounter(QObject):
        def eventFilter(self, obj, event):
            if start[0] is not None and event.type() == QEvent.Type.Paint:
                paints[0] += 1
            return False

    counter = PaintCounter()
    graph.viewport().installEventFilter(counter)
    last = [None]
    heartbeat = QTimer()
    def tick():
        now = time.perf_counter()
        if last[0] is not None:
            gaps.append((now - last[0]) * 1000)
        last[0] = now
    heartbeat.timeout.connect(tick)
    heartbeat.start(10)
    batches = [0]
    worker = [None]

    def produce():
        try:
            for batch in range(round(options.seconds * 100)):
                delay = start[0] + batch * 0.01 - time.perf_counter()
                if delay > 0:
                    time.sleep(delay)
                receipt = ingest(frames(10000 + batch * 10, 10))
                if receipt is not None:
                    receipt.result(timeout=10)
                batches[0] += 1
        except Exception as exc:
            errors.append(str(exc))

    def begin():
        paints[0], gaps[:] = 0, []
        start[0], cpu_start[0] = time.perf_counter(), time.process_time()
        worker[0] = threading.Thread(target=produce)
        worker[0].start()
        QTimer.singleShot(round(options.seconds * 1000) + 300, finish)

    def finish():
        elapsed = time.perf_counter() - start[0]
        worker[0].join(timeout=5)
        report = {
            "implementation": "COMTool original Graph_Plot" if options.legacy else "COMTool-x decoder + plot",
            "python": platform.python_version(), "pyqtgraph": pg.__version__,
            "qt_binding": os.environ["PYQTGRAPH_QT_LIB"], "platform": os.environ.get("QT_QPA_PLATFORM", "default"),
            "channels": options.channels, "samples_per_channel_per_second": 1000,
            "preloaded_per_channel": 10000, "window_seconds": 10, "canvas": [1200, 650],
            "line_width": line_width, "antialias": antialias, "style": options.style,
            "acquisition_batch_ms": 10,
            "configured_new_refresh_ms": 16 if not options.legacy else None,
            "elapsed_seconds": round(elapsed, 3), "acquisition_batches": batches[0],
            "retained_points": retained(), "paint_events_per_second": round(paints[0] / elapsed, 2),
            "cpu_seconds": round(time.process_time() - cpu_start[0], 3),
            "heartbeat_p95_ms": round(sorted(gaps)[min(len(gaps) - 1, int(len(gaps) * .95))], 2) if gaps else None,
            "heartbeat_max_ms": round(max(gaps), 2) if gaps else None,
            "errors": errors,
        }
        options.output.parent.mkdir(parents=True, exist_ok=True)
        options.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        print(json.dumps(report, ensure_ascii=False, indent=2))
        if controller:
            controller.engine.close()
        application.quit()
    QTimer.singleShot(600, begin)
    application.exec()


if __name__ == "__main__":
    main()
