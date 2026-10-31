"""Synthetic signal generation and numerical RMS, with no hardware side effects."""
import math


def validate(config, context):
    """Validate the whole candidate so min/max can change in one transaction."""
    if config["minimum"] >= config["maximum"]:
        raise ValueError("下界必须小于上界")
    return config


def validate_view(properties, context):
    """Appearance follows the same checked path for QML and API callers."""
    if not properties["display_label"].strip():
        raise ValueError("显示名称不能为空")
    return properties


def describe_ports(config, context):
    """Changing channels is an actual graph-contract change, not a view setting."""
    ports = [{"id": f"ch{i+1}", "direction": "out", "data_type": "number"}
             for i in range(config["channels"])]
    return ports + [{"id": "samples", "direction": "out", "data_type": "samples"},
                    {"id": "rms", "direction": "out", "data_type": "number"}]


def signal_values(config, seconds):
    """Phase-shift each channel while keeping every value inside the bounds."""
    center = (config["maximum"] + config["minimum"]) / 2
    amplitude = (config["maximum"] - config["minimum"]) / 2
    return {f"ch{i+1}": center + amplitude * math.sin(
                2 * math.pi * config["frequency"] * seconds + i * math.pi / 4)
            for i in range(config["channels"])}


def generate(context):
    """A package worker generates data; the host still owns pause and routing."""
    seconds, count = 0.0, 0
    while True:
        context.checkpoint()
        config = context.config
        values = signal_values(config, seconds)
        context.publish({**values, "samples": {"time": seconds, "values": values}})
        count += 1
        context.observe({"generated": count, "signal_time": seconds})
        interval = config["period_ms"] / 1000
        context.sleep(interval)
        seconds += interval


def on_start(context):
    """The producer starts only with the host's real running session."""
    context.background(generate, name="多路合成信号")


def process(inputs, state):
    """A manual source step computes one frame while the producer is paused."""
    config = inputs["__config__"]
    seconds = state.get("manual_steps", 0) * config["period_ms"] / 1000
    values = signal_values(config, seconds)
    state["manual_steps"] = state.get("manual_steps", 0) + 1
    return {**values, "samples": {"time": seconds, "values": values}}


def action_measure(inputs, state, context):
    """Numerically integrate one period, reporting progress and cancellation."""
    steps = inputs["args"]["steps"]
    config = inputs["__config__"]
    squares = 0.0
    for index in range(steps):
        context.sleep(0.01)
        value = signal_values(config, index / (steps * config["frequency"]))["ch1"]
        squares += value * value
        context.progress((index + 1) / steps, f"已计算 {index+1}/{steps} 个点")
    rms = math.sqrt(squares / steps)
    state["last_rms"] = rms
    state["measurements"] = state.get("measurements", 0) + 1
    return {"result": {"rms": rms, "samples": steps}, "outputs": {"rms": rms}}
