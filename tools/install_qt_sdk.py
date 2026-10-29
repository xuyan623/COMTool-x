"""Install the matching Qt SDK from Qt's published package manifest.

This is a development tool, not part of the delivered application. Archives stay
in the existing D: installer hierarchy; each archive is verified before extraction.
"""

from concurrent.futures import ThreadPoolExecutor
from hashlib import sha1
from pathlib import Path
import subprocess
import time
from urllib.request import urlopen
from xml.etree import ElementTree

VERSION = "6.12.0"
BASE = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6120/qt6_6120_msvc2022_64/"
DESTINATION = Path("D:/Software/DevTools/Qt") / VERSION / "msvc2022_64"
ARCHIVES = Path("D:/Software/Installers/Qt/6.12.0-msvc2022-x64")
MODULES = {"qtbase", "qtdeclarative", "qtsvg", "qttranslations", "qtserialport", "qtshadertools", "qt5compat"}


def retrieve(url):
    """Retry transient network failures; permanent failure stops installation."""
    for attempt in range(4):
        try:
            with urlopen(url, timeout=90) as response:
                return response.read()
        except Exception:
            if attempt == 3:
                raise
            time.sleep(attempt + 1)


def install(entry):
    """Verify the official checksum, then extract the archive in the SDK root."""
    package, version, filename = entry
    url = BASE + package + "/" + version + filename
    expected = retrieve(url + ".sha1").decode("ascii").strip().split()[0].lower()
    if len(expected) != 40 or any(c not in "0123456789abcdef" for c in expected):
        raise ValueError("Invalid official SHA-1 checksum for " + filename)
    path = ARCHIVES / filename
    if not path.is_file() or sha1(path.read_bytes()).hexdigest() != expected:
        print("Downloading " + filename, flush=True)
        # Select a reachable regional Qt mirror. The downloaded bytes still
        # have to match the checksum retrieved from Qt's official manifest host.
        mirror = url.replace("https://download.qt.io/", "https://mirrors.bfsu.edu.cn/qt/")
        part = path.with_suffix(path.suffix + ".part")
        downloaded = False
        for source in (mirror, url):
            completed = subprocess.run(["curl.exe", "--fail", "--location", "--silent", "--show-error",
                "--max-time", "300", "--speed-limit", "10000", "--speed-time", "30",
                "--retry", "2", "--output", str(part), source])
            if completed.returncode == 0:
                downloaded = True
                break
        if not downloaded:
            raise RuntimeError("Qt archive download failed: " + filename)
        data = part.read_bytes()
        if sha1(data).hexdigest() != expected:
            raise ValueError("Qt archive checksum mismatch: " + filename)
        part.replace(path)
    subprocess.run(["tar.exe", "-xf", str(path), "-C", str(DESTINATION)], check=True)
    print("Verified and extracted " + filename, flush=True)


def main():
    """Select only the modules required by this native desktop workbench."""
    root = ElementTree.fromstring(retrieve(BASE + "Updates.xml"))
    entries = []
    for package in root.findall("PackageUpdate"):
        name = package.findtext("Name", "")
        if not name.endswith("win64_msvc2022_64") or "debug" in name:
            continue
        for filename in package.findtext("DownloadableArchives", "").split(","):
            filename = filename.strip()
            if filename and (filename.split("-Windows")[0] in MODULES or filename.startswith(("d3dcompiler_47", "opengl32sw"))):
                entries.append((name, package.findtext("Version"), filename))
    if not entries:
        raise RuntimeError("No matching Qt SDK packages were found")
    DESTINATION.mkdir(parents=True, exist_ok=True)
    ARCHIVES.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=3) as pool:
        for _ in pool.map(install, entries):
            pass
    prefix = DESTINATION
    if not (prefix / "bin/qmake.exe").is_file():
        raise RuntimeError("Qt archive extraction did not produce the expected SDK")
    # Qt supports relocation through qt.conf rather than rewriting package files.
    (prefix / "bin/qt.conf").write_text("[Paths]\nPrefix=..\n", encoding="utf-8")
    print("Qt SDK ready: " + str(prefix), flush=True)


if __name__ == "__main__":
    main()
