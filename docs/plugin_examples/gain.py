"""External plugin example: multiply a numeric input and publish the result."""

from comtoolx.sdk import Definition, NodeRuntime, Port


class Gain(NodeRuntime):
    """Own processing state; the shell knows only this plugin's declared contract."""

    def receive(self, port, data):
        value = float(data) * self.config["gain"]
        with self.lock:
            self.state["value"] = value
        return self.emit("value", value)


def gain_view(runtime, request, properties):
    """Present runtime state without triggering any device operation."""
    from PySide6.QtCore import QTimer
    from PySide6.QtWidgets import QLabel
    label = QLabel()
    label.setObjectName(runtime.id + ".result")
    def refresh():
        value = runtime.snapshot().get("value")
        label.setText(properties.get("prefix", "结果：") + ("—" if value is None else f"{value:.6g}"))
    timer = QTimer(label)
    timer.timeout.connect(refresh)
    timer.start(100)
    label.refresh_from_runtime = refresh
    refresh()
    return label


def register(registry):
    """Register one real processing node, its editable property and optional view."""
    registry.register(Definition(
        "example.gain", "数值增益", "示例", "输入值乘以 gain，输出处理结果",
        (Port("in", "输入", "in", "number"), Port("value", "输出", "out", "number")),
        {"gain": {"type": "number", "default": 2.0, "min": -1000000, "max": 1000000}},
        Gain, {"result": gain_view},
        view_properties={"result": {"prefix": {"type": "string", "default": "结果："}}}))
