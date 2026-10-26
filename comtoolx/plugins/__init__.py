"""Built-in plugin registration; domain implementations stay outside the shell."""

from . import basic, layouts, plotting, protocols, terminal, transports, wires
from pathlib import Path


def register_builtins(registry):
    """Register the shipped library through the same interface as external plugins."""
    for module in (layouts, wires, transports, protocols, basic, plotting, terminal):
        source = Path(module.__file__)
        if not source.is_file():
            source = Path(__file__).parent.parent / "plugin_sources" / (module.__name__.rsplit(".", 1)[-1] + ".py")
        registry.register_provider(module, source, builtin=True)
