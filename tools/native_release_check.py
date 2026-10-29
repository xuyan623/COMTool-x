"""Validate a deployed native build with no developer Qt/Python on PATH.

Python is only the external test driver. The application starts without a
selected interpreter, and all public examples execute only official C++ nodes.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from native_ui_check import Client


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--data-dir', type=Path, required=True)
    args = parser.parse_args()
    executable = args.executable.resolve()
    deployment = executable.parent
    data = args.data_dir.resolve()
    output = args.output.resolve()
    data.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    if (data / '.sessions/api.json').exists():
        raise RuntimeError('Use a dedicated data directory with no running instance')
    environment = os.environ.copy()
    environment['PATH'] = os.pathsep.join([str(Path(os.environ['SystemRoot']) / 'System32'), os.environ['SystemRoot']])
    for name in ('PYTHONHOME', 'PYTHONPATH', 'QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'QT_QPA_PLATFORM'):
        environment.pop(name, None)
    processes, checks = [], []

    def check(condition, message):
        if not condition:
            raise AssertionError(message)
        checks.append(message)
        print('PASS', message, flush=True)

    def launch():
        # A visible GUI is required to validate actual QML rendering/captures.
        process = subprocess.Popen([str(executable), '--data-dir', str(data)], env=environment)
        processes.append(process)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError('Deployed application exited: ' + str(process.returncode))
            try:
                client = Client(data)
                if json.loads((data / '.sessions/api.json').read_text('utf-8'))['pid'] == process.pid:
                    client.call('schema')
                    return client
            except (OSError, ValueError, RuntimeError):
                pass
            time.sleep(.05)
        raise TimeoutError('Deployed application discovery')

    c = None
    try:
        c = launch()
        check(executable.name == 'CoRig.exe' and (deployment / 'CoRig-cli.exe').is_file(), 'deployed GUI and CLI use the CoRig product names')
        check(c.call('app.state')['application'] == c.call('schema')['application'] == 'CoRig', 'GUI state and API schema identify the current CoRig application')
        check(c.call('app.state')['python'] == '', 'deployed application starts without a Python interpreter')
        check(len(c.call('registry')) == 78, 'all 78 official plugin declarations are available in deployment')
        for name in ('SerialPlot', 'FunctionLab', 'RecordPlayback'):
            c.call('project.open', {'path': str(deployment / 'examples' / (name + '.corig'))})
            check(c.call('project.validate')['valid'], name + ' opens as a native v3 project')
            check(not c.call('graph.overlaps'), name + ' has no node spacing warnings')
            c.call('workspace.open', {'page': 'runtime'})
            c.call('runtime.start', {'debug': False})
            if name == 'SerialPlot':
                c.call('node.action', {'node': 'serial', 'action': 'connect'})
                c.call('node.action', {'node': 'console', 'action': 'send', 'args': {'text': '1,2,3'}})
            time.sleep(.6)
            state = c.call('runtime.state')
            check(not state['errors'] and not state['failed'], name + ' computes with no Python selected')
            if name in ('FunctionLab', 'RecordPlayback'):
                check(state['nodes']['plot']['executions'] >= 30 and state['nodes']['attitude']['state']['valid'],
                      'deployed curve and aircraft receive live native data')
            else:
                check(c.call('node.action', {'node': 'plot', 'action': 'read_data'})['channels']['X']['total'] == 1,
                      'deployed serial-console-codec-curve loop delivers an actual frame')
            c.call('capture', {'target': 'window', 'path': str(output / (name + '.png'))})
            c.call('runtime.stop')
        # The second process shares discovery storage but not the first
        # process's session. CLI must find surviving authenticated records.
        record = json.loads((data / '.sessions/api.json').read_text('utf-8'))
        record.pop('application')
        unbranded = data / '.sessions/unbranded.json'
        unbranded.write_text(json.dumps(record), 'utf-8')
        rejected = subprocess.run([str(deployment / 'CoRig-cli.exe'), 'api', 'app.state', '--discovery', str(unbranded)],
                                  env=environment, capture_output=True, encoding='utf-8')
        unbranded.unlink()
        check(rejected.returncode != 0 and not json.loads(rejected.stdout)['ok'], 'CLI rejects previous unbranded discovery records without connecting')
        other = launch()
        alias = data / '.sessions/api.json'
        alias.unlink()
        cli = subprocess.run([str(deployment / 'CoRig-cli.exe'), 'api', 'app.state', '--data-dir', str(data)],
                             env=environment, capture_output=True, encoding='utf-8', check=True)
        check(json.loads(cli.stdout)['ok'], 'native CLI recovers when the default discovery record is absent')
        other.call('app.quit')
        processes[-1].wait(timeout=10)
        check(c.call('app.state')['project'] == 'RecordPlayback', 'closing another instance preserves the original session and API')
        c.call('app.quit')
        processes[0].wait(timeout=10)
        c = launch()
        # Run full visual and local SSH/Python integration against the actual
        # packaged executable and its deployed plugins, not the source binary.
        tools = Path(__file__).resolve().parent
        for script, subfolder, extra in (
            ('native_ui_check.py', 'interface', []),
            ('native_workbench_check.py', 'workbench', []),
            ('native_instrument_check.py', 'instruments', ['--python', sys.executable]),
            ('native_animation_check.py', 'animation', []),
            ('native_recording_check.py', 'recording', [])):
            subprocess.run([sys.executable, str(tools / script), '--data-dir', str(data), '--output', str(output / subfolder)] + extra,
                           env=environment, check=True)
            check(True, script + ' passes against the deployed executable')
        # The AI audit owns its separate instance and verifies every command,
        # including app.quit, without disturbing this deployment-check session.
        subprocess.run([sys.executable, str(tools / 'native_ai_check.py'), '--executable', str(executable),
                        '--data-dir', str(data / 'ai'), '--output', str(output / 'ai')], env=environment, check=True)
        check(True, 'all public AI commands and unnamed QML controls pass against the deployed executable')
        c.call('app.quit')
        processes[-1].wait(timeout=10)
        c = None
        unexpected = [p.name for p in deployment.rglob('*') if p.is_file() and
                      (p.name.lower().startswith(('python3', 'pyside6')) or p.name.lower() == 'python.exe')]
        check(not unexpected, 'release directory contains no bundled Python or PySide6 runtime')
        # The application creates this file only when Qt emits a message.
        # A clean startup/exit with no warnings legitimately leaves no file.
        log_path = data / 'native.log'
        logs = log_path.read_text('utf-8') if log_path.exists() else ''
        check('ReferenceError' not in logs and 'TypeError' not in logs and 'binding loop' not in logs.lower(),
              'packaged views open, reopen and exit without QML reference/type errors')
        report = {'checks': checks, 'count': len(checks), 'executable': str(executable), 'clean_path': True}
        (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), 'utf-8')
    finally:
        if c is not None:
            try:
                if c.call('app.state')['dirty']:
                    c.call('project.save', {'path': str(output / 'LastCheck.corig')})
                c.call('runtime.stop')
                c.call('app.quit')
            except (OSError, RuntimeError):
                pass
        for process in processes:
            if process.poll() is None:
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=5)


if __name__ == '__main__':
    main()
