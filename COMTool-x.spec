# -*- mode: python ; coding: utf-8 -*-
"""Build GUI and command-line entry points sharing one portable Python runtime."""

from pathlib import Path
from PyInstaller.utils.hooks import collect_submodules

root = Path(SPECPATH)
analysis = Analysis(
    [str(root / "launch.py")], pathex=[str(root)],
    binaries=[], datas=[(str(root / "comtoolx" / "assets"), "comtoolx/assets")]
        + [(str(path), "comtoolx/plugin_sources") for path in (root / "comtoolx/plugins").glob("*.py")],
    hiddenimports=collect_submodules("serial.urlhandler") + ["concurrent.futures.thread"],
    hookspath=[], hooksconfig={}, runtime_hooks=[],
    excludes=["PyQt5", "PyQt6", "tkinter", "pytest", "numpy.f2py", "numpy.testing"],
    noarchive=False)
archive = PYZ(analysis.pure)
common = dict(exclude_binaries=True, debug=False, bootloader_ignore_signals=False,
              strip=False, upx=False, icon=str(root / "comtoolx/assets/icon.ico"))
gui = EXE(archive, analysis.scripts, [], name="COMTool-x", console=False, **common)
cli = EXE(archive, analysis.scripts, [], name="COMTool-x-cli", console=True, **common)
collection = COLLECT(gui, cli, analysis.binaries, analysis.datas,
                     strip=False, upx=False, name="COMTool-x")
