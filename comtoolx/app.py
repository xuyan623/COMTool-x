"""Source and portable-executable entry point, including the local API client."""

from __future__ import annotations

import argparse
import json
import logging
import os
import sys
import time
from pathlib import Path
from urllib.request import Request, urlopen
from urllib.error import HTTPError


def application_root():
    """Resolve writable portable data independently of the working directory."""
    return Path(sys.executable).resolve().parent if getattr(sys, "frozen", False) else Path(__file__).resolve().parent.parent


def api_client(argv):
    """The packaged EXE can call its running instance without a separate Python install."""
    parser = argparse.ArgumentParser(prog="COMTool-x api")
    parser.add_argument("op")
    parser.add_argument("--json", default="{}")
    parser.add_argument("--json-file", type=Path)
    parser.add_argument("--data-dir", type=Path, default=application_root() / ".sessions")
    parser.add_argument("--wait", action="store_true")
    parser.add_argument("--timeout", type=float, default=60)
    options = parser.parse_args(argv)
    from .api import discover_api
    try:
        info = discover_api(options.data_dir)
        args = json.loads(options.json_file.read_text(encoding="utf-8") if options.json_file else options.json)
        body = json.dumps({"op": options.op, "args": args}, ensure_ascii=False).encode("utf-8")
        headers = {"Authorization": "Bearer " + info["token"], "Content-Type": "application/json"}
        request = Request(info["url"] + "/v1/command", data=body, headers=headers, method="POST")
        with urlopen(request, timeout=options.timeout) as response:
            result = json.load(response)
        job = result.get("result", {}).get("job_id") if isinstance(result.get("result"), dict) else None
        deadline = time.monotonic() + options.timeout
        while options.wait and job and time.monotonic() < deadline:
            poll = Request(info["url"] + "/v1/jobs/" + job, headers=headers)
            with urlopen(poll, timeout=20) as response:
                result = json.load(response)
            if result["result"]["status"] != "running":
                break
            time.sleep(0.05)
        print(json.dumps(result, ensure_ascii=False, indent=2))
        if job and options.wait and result["result"].get("status") == "running":
            return 2
        failed = isinstance(result.get("result"), dict) and result["result"].get("status") == "failed"
        return 0 if result.get("ok") and not failed else 1
    except HTTPError as exc:
        print(exc.read().decode("utf-8"))
        return 1
    except (OSError, ValueError) as exc:
        print(json.dumps({"ok": False, "error": str(exc)}, ensure_ascii=False))
        return 1


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv and argv[0] == "api":
        return api_client(argv[1:])
    parser = argparse.ArgumentParser(prog="COMTool-x")
    parser.add_argument("project_file", nargs="?")
    parser.add_argument("--project")
    parser.add_argument("--read-only", action="store_true")
    parser.add_argument("--data-dir", type=Path, default=application_root() / ".sessions")
    parser.add_argument("--api-port", type=int, default=0)
    options = parser.parse_args(argv)
    options.data_dir.mkdir(parents=True, exist_ok=True)
    logging.basicConfig(filename=options.data_dir / "run.log", level=logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s", encoding="utf-8")
    from PySide6.QtWidgets import QApplication
    from PySide6.QtGui import QIcon
    from .sdk import Registry
    from .plugins import register_builtins
    from .controller import Controller
    from .ui.window import MainWindow
    from .api import ApiServer
    application = QApplication([sys.argv[0]])
    application.setApplicationName("COMTool-x")
    application.setOrganizationName("COMTool-x")
    application.setWindowIcon(QIcon(str(Path(__file__).parent / "assets" / "icon.svg")))
    registry = Registry()
    register_builtins(registry)
    controller = Controller(registry, options.data_dir, application_root() / "project")
    window = MainWindow(controller)
    project = options.project or options.project_file
    if project:
        try:
            controller.command("project.open", {"path": project, "read_only": options.read_only})
        except Exception as exc:
            window.show_notice("error", f"打开工程失败：{exc}")
    server = ApiServer(controller, options.api_port)
    server.start()
    window.api_info = {**server.info, "discovery": str(server.instance_path),
                       "default_discovery": str(server.info_path)}
    application.aboutToQuit.connect(server.close)
    def exception_hook(exc_type, value, traceback):
        logging.error("Uncaught exception", exc_info=(exc_type, value, traceback))
        controller.notice.emit("error", str(value))
    sys.excepthook = exception_hook
    window.show()
    logging.info("Started %s", sys.version)
    return application.exec()


if __name__ == "__main__":
    raise SystemExit(main())
