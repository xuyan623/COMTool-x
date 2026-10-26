"""Exercise discovery with real GUI processes, shared data, graceful exit and an owned crash.

Only processes launched by this harness are controlled. Reports never contain
API credentials, and the blank test projects never connect to hardware.
"""

from __future__ import annotations

import argparse
import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

from tools.integration_check import Client

ROOT = Path(__file__).resolve().parent.parent


def main():
    """Verify both discovery ownership and CLI recovery against the selected real runtime."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path)
    options = parser.parse_args()
    artifacts = ROOT / "verification/api-discovery" / ("packaged" if options.executable else "source")
    data = artifacts / "session"
    data.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment["QT_QPA_PLATFORM"] = "offscreen"
    if options.executable:
        environment["PATH"] = str(Path(os.environ["SystemRoot"]) / "System32")
        environment.pop("PYTHONPATH", None)
        environment.pop("PYTHONHOME", None)
    executable = [str(options.executable.resolve())] if options.executable else [sys.executable, "-m", "comtoolx"]
    cli = ([str(options.executable.resolve().with_name("COMTool-x-cli.exe"))] if options.executable
           else [sys.executable, "-m", "comtoolx"])
    owned = []
    report = {}
    with (artifacts / "process.log").open("w", encoding="utf-8") as log:
        def start_window(number):
            """Identify the child by its newly published PID or Python launcher parent."""
            process = subprocess.Popen(executable + ["--data-dir", str(data)], cwd=ROOT,
                                       env=environment, stdout=log, stderr=subprocess.STDOUT)
            record = {"process": process, "client": None}
            owned.append(record)
            deadline = time.monotonic() + 25
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError("Test GUI exited during startup; see process.log")
                try:
                    info = json.loads((data / "api.json").read_text(encoding="utf-8"))
                    if process.pid in (info.get("pid"), info.get("parent_pid")):
                        client = Client(data)
                        record["client"] = client
                        record["entry"] = data / "instances" / f"api-{info['pid']}-{info['url'].rsplit(':', 1)[-1]}.json"
                        assert record["entry"].is_file()
                        client.command("project.new", name=f"Discovery test {number}")
                        return record
                except (OSError, ValueError):
                    pass
                time.sleep(.1)
            raise TimeoutError("GUI did not publish its independent API record")

        def wait_for_alias(record):
            """Allow the periodic repair to run, while requiring a complete authenticated record."""
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                try:
                    info = json.loads((data / "api.json").read_text(encoding="utf-8"))
                    if info == record["client"].info and record["entry"].exists():
                        return
                except (OSError, ValueError):
                    pass
                time.sleep(.1)
            raise AssertionError("A surviving GUI did not recover discovery")

        try:
            first, second = start_window(1), start_window(2)
            second["client"].command("app.quit", discard=True)
            second["process"].wait(timeout=10)
            wait_for_alias(first)
            assert Client(data).command("app.state")["name"] == "Discovery test 1"
            assert not second["entry"].exists()
            report["newer_close"] = "older GUI remains discoverable and authenticated"

            third = start_window(3)
            first["client"].command("app.quit", discard=True)
            first["process"].wait(timeout=10)
            wait_for_alias(third)
            assert Client(data).command("app.state")["name"] == "Discovery test 3"
            report["older_close"] = "newer GUI keeps its own discovery record"

            (data / "api.json").unlink()
            third["entry"].unlink()
            wait_for_alias(third)
            assert Client(data).command("app.state")["name"] == "Discovery test 3"
            report["deleted_files"] = "live GUI restored both its independent record and default discovery"

            fourth = start_window(4)
            actual_pid = fourth["client"].info["pid"]
            assert fourth["process"].pid in (actual_pid, fourth["client"].info.get("parent_pid"))
            # Crash only the child verified above; a Windows venv may use a launcher PID.
            if actual_pid == fourth["process"].pid:
                fourth["process"].kill()
            else:
                os.kill(actual_pid, signal.SIGTERM)
            fourth["process"].wait(timeout=10)
            result = subprocess.run(cli + ["api", "app.state", "--data-dir", str(data)], cwd=ROOT,
                                    env=environment, capture_output=True, encoding="utf-8", timeout=15)
            assert result.returncode == 0, result.stderr
            assert json.loads(result.stdout)["result"]["name"] == "Discovery test 3"
            wait_for_alias(third)
            report["crash_and_cli"] = "CLI ignored crashed owner's stale record and reached surviving GUI"

            third["client"].command("app.quit", discard=True)
            third["process"].wait(timeout=10)
            assert not (data / "api.json").exists()
            result = subprocess.run(cli + ["api", "app.state", "--data-dir", str(data)], cwd=ROOT,
                                    env=environment, capture_output=True, encoding="utf-8", timeout=15)
            assert result.returncode == 1
            assert json.loads(result.stdout)["ok"] is False
            assert "Traceback" not in result.stderr
            report.update(no_window="actionable JSON error without traceback", status="passed", hardware_validation=False)
            (artifacts / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
            print(json.dumps(report, indent=2))
        finally:
            for record in reversed(owned):
                process, client = record["process"], record["client"]
                if process.poll() is None:
                    if client is not None:
                        client.command("app.quit", discard=True)
                    else:
                        process.terminate()
                    process.wait(timeout=10)


if __name__ == "__main__":
    main()
