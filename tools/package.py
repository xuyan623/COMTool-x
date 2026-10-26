"""Build a portable application and archive only the intentional release files."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive-only", action="store_true")
    parser.add_argument("--dist-dir", type=Path, default=ROOT / "build" / "release")
    options = parser.parse_args()
    output = options.dist_dir.resolve()
    release = output / "COMTool-x"
    if not options.archive_only:
        from PySide6.QtCore import Qt, QRectF
        from PySide6.QtGui import QImage, QPainter
        from PySide6.QtSvg import QSvgRenderer
        from PySide6.QtWidgets import QApplication
        application = QApplication.instance() or QApplication([])
        icon = QImage(256, 256, QImage.Format.Format_ARGB32)
        icon.fill(Qt.GlobalColor.transparent)
        painter = QPainter(icon)
        QSvgRenderer(str(ROOT / "comtoolx/assets/icon.svg")).render(painter, QRectF(0, 0, 256, 256))
        painter.end()
        if not icon.save(str(ROOT / "comtoolx/assets/icon.ico")):
            raise RuntimeError("Unable to create Windows icon")
        # BUILD COMPAT BEGIN: PyInstaller 6.22 resolves Qt 6.12's Windows ICU
        # dependency from PATH before System32. An unrelated Poppler ICU exports
        # different symbols and breaks startup. Build with only Python/OS search
        # paths; remove this isolation if upstream dependency resolution changes.
        environment = os.environ.copy()
        environment["PATH"] = os.pathsep.join([
            sys.base_prefix, str(Path(sys.base_prefix) / "DLLs"),
            str(Path(os.environ["SystemRoot"]) / "System32")])
        environment["PYINSTALLER_CONFIG_DIR"] = str(ROOT / ".cache/pyinstaller")
        subprocess.run([sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean",
                        "--distpath", str(output), str(ROOT / "COMTool-x.spec")],
                       cwd=ROOT, env=environment, check=True)
        # BUILD COMPAT END
    references = ROOT / "skills/comtool-x/references"
    references.mkdir(parents=True, exist_ok=True)
    for name in ("API.md", "PLUGIN_SDK.md"):
        shutil.copy2(ROOT / "docs" / name, references / name)
    for name in ("docs", "skills"):
        shutil.copytree(ROOT / name, release / name,
                        ignore=shutil.ignore_patterns("__pycache__", "*.pyc"), dirs_exist_ok=True)
    # Official distribution includes only the declared generic starter project.
    (release / "examples").mkdir(exist_ok=True)
    shutil.copy2(ROOT / "examples/SerialPlot.comtoolx", release / "examples/SerialPlot.comtoolx")
    for name in ("README.md", "LICENSE", "THIRD_PARTY_NOTICES.md"):
        shutil.copy2(ROOT / name, release / name)
    (release / "plugins").mkdir(exist_ok=True)
    (release / "project").mkdir(exist_ok=True)
    reports = release / "verification"
    reports.mkdir(exist_ok=True)
    for name in ("source", "packaged"):
        report = ROOT / "verification" / name / "report.json"
        if report.exists():
            shutil.copy2(report, reports / f"{name}.json")
        discovery = ROOT / "verification/api-discovery" / name / "report.json"
        if discovery.exists():
            shutil.copy2(discovery, reports / f"api-discovery-{name}.json")
    benchmark = ROOT / "verification" / "plot_benchmark.json"
    if benchmark.exists():
        shutil.copy2(benchmark, reports / benchmark.name)
    archive = shutil.make_archive(str(output / "COMTool-x-Windows-x64"),
                                  "zip", root_dir=output, base_dir="COMTool-x")
    print(archive)
    source_archive = output / "COMTool-x-source.zip"
    with zipfile.ZipFile(source_archive, "w", zipfile.ZIP_DEFLATED) as package:
        for name in ("comtoolx", "examples/SerialPlot.comtoolx", "docs", "skills", "tools", "tests",
                     "README.md", "LICENSE", "THIRD_PARTY_NOTICES.md", "pyproject.toml",
                     "uv.lock", "launch.py", "build.ps1", "COMTool-x.spec", ".gitignore"):
            source = ROOT / name
            for file in source.rglob("*") if source.is_dir() else [source]:
                if file.is_file() and "__pycache__" not in file.parts and file.suffix != ".pyc":
                    package.write(file, Path("COMTool-x") / file.relative_to(ROOT))
    print(source_archive)


if __name__ == "__main__":
    main()
