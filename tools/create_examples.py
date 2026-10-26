"""Generate the single official serial-to-plot starter project."""

from pathlib import Path

from comtoolx.sdk import Registry, checked_properties
from comtoolx.plugins import register_builtins
from comtoolx.model import new_project, validate
from comtoolx.projectio import save_bundle

ROOT = Path(__file__).resolve().parent.parent


def main():
    """Use generic plugin defaults without reading machine-specific settings."""
    registry = Registry()
    register_builtins(registry)
    project = new_project("串口与实时曲线")
    for ident, plugin, name, x in (
        ("serial", "io.serial", "串口", 0),
        ("decoder", "protocol.plot", "曲线协议", 320),
        ("plot", "plot.curves", "实时曲线", 640),
    ):
        definition = registry.nodes[plugin]
        project["nodes"].append({
            "id": ident, "plugin": plugin, "name": name, "position": [x, 100],
            "config": checked_properties(definition.properties, {})})
    for ident, source, out_port, target, in_port in (
        ("bytes", "serial", "rx", "decoder", "bytes"),
        ("samples", "decoder", "samples", "plot", "samples"),
    ):
        project["wires"].append({
            "id": ident, "plugin": "wire.direct",
            "source": {"node": source, "port": out_port},
            "target": {"node": target, "port": in_port},
            "properties": {"color": "#6CAEE8", "width": 2, "style": "curve"}})
    project["ui"]["items"] = [
        {"id": "root", "plugin": "layout.row", "parent": None, "title": "",
         "stretch": 1, "properties": {"spacing": 10, "margin": 8}},
        {"id": "connection", "node": "serial", "view": "connection", "parent": "root",
         "title": "连接", "stretch": 1, "properties": {"max_width": 300}},
        {"id": "plot_ui", "node": "plot", "view": "plot", "parent": "root",
         "title": "实时曲线", "stretch": 4, "properties": {}},
    ]
    assert not validate(project, registry), validate(project, registry)
    directory = ROOT / "examples"
    directory.mkdir(exist_ok=True)
    save_bundle(project, registry, directory / "SerialPlot.comtoolx", ROOT)
    print("Created: SerialPlot.comtoolx")


if __name__ == "__main__":
    main()
