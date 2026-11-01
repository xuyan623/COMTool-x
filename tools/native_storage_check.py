"""Regression checks for real CoRig cache placement and scoped self-uninstall.

Uses private copies of the deployed application. Never removes the source
release or user installation, and never connects to a hardware device.
"""
from __future__ import annotations
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
from urllib.request import Request, urlopen
from native_ui_check import Client


def wait_for(predicate, seconds=30):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(.05)
    raise TimeoutError('Lifecycle condition did not complete')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--release', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    release = args.release.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    run = output / time.strftime('run-%Y%m%d-%H%M%S')
    run.mkdir()
    checks = []
    environment = os.environ.copy()
    for key in ('QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'QT_QPA_PLATFORM'):
        environment.pop(key, None)
    environment['PATH'] = os.pathsep.join([str(Path(os.environ['SystemRoot']) / 'System32'), os.environ['SystemRoot']])

    def check(condition, label):
        if not condition:
            raise AssertionError(label)
        checks.append(label)
        print('PASS', label, flush=True)

    def clone(name):
        target = run / name
        shutil.copytree(release, target, ignore=shutil.ignore_patterns('cache', '.sessions'))
        return target

    def uninstall(target, mode):
        # Reports are explicitly outside the private installation being removed.
        assert target.parent == run and target != release
        report = run / (target.name + '-uninstall.json')
        result = subprocess.run([str(target / 'CoRig-uninstall.exe'), '--silent', mode, '--report', str(report)],
                                env=environment, capture_output=True, timeout=75)
        check(result.returncode == 0, mode + ' uninstaller successfully acknowledges its cleanup worker')
        wait_for(report.is_file, seconds=60)
        return json.loads(report.read_text('utf-8-sig'))

    # Real Quick rendering and recording exercise both explicit and implicit
    # Qt caches with a deliberately different external data directory.
    target = clone("中文 [literal] O'Brien")
    external = run / 'external-data'
    external.mkdir()
    log = (run / 'application.log').open('wb')
    process_start = time.time()
    process = subprocess.Popen([str(target / 'CoRig.exe'), '--data-dir', str(external)], env=environment, stdout=log, stderr=log)
    client = None
    try:
        wait_for(lambda: (external / '.sessions/api.json').is_file())
        client = Client(external)
        check(not client.call('app.state')['dirty'], 'private installed application starts with a clean design')
        client.call('registry')
        client.call('project.get')
        with urlopen(Request(client.url + '/v3/schema', headers={'Authorization': 'Bearer ' + client.token}), timeout=10) as response:
            schema = json.load(response)
        check(schema['result']['application'] == 'CoRig', 'authenticated schema identifies the real CoRig interface')
        client.call('graph.node.add', {'id': 'clock', 'plugin': 'source.time', 'config': {'period_ms': 10, 'outputs': [{'name': 't', 'clock': 'elapsed', 'unit': 's'}]}})
        client.call('graph.node.add', {'id': 'record', 'plugin': 'data.record', 'config': {'data_type': 'number'}})
        client.call('ui.view.add', {'id': 'recorder_view', 'node': 'record', 'view': 'recorder',
                                   'properties': {'x': 16, 'y': 16, 'width': 416, 'height': 128}})
        client.call('workspace.open', {'page': 'layout'})
        client.call('graph.wire.add', {'id': 'capture', 'source': ['clock', 't'], 'target': ['record', 'in']})
        client.call('runtime.start')
        client.call('node.action', {'node': 'record', 'action': 'start'})
        wait_for(lambda: list((target / 'cache/temp').glob('CoRig-recording-*')))
        check(True, 'live recording uses temporary storage inside the installation')
        time.sleep(.3)
        client.call('node.action', {'node': 'record', 'action': 'pause'})
        capture = external / 'capture.corigrec'
        client.call('node.action', {'node': 'record', 'action': 'save', 'args': {'path': str(capture)}})
        check(capture.is_file(), 'real sampled recording saves to the explicitly selected external path')
        client.call('runtime.stop')
        client.call('project.save', {'path': str(external / 'project/test.crpj')})
        client.call('app.quit')
        process.wait(timeout=15)
        client = None
    finally:
        if process.poll() is None:
            if client:
                try: client.call('app.quit', {'discard': True})
                except Exception: pass
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
        log.close()
    check((target / 'cache/logs/native.log').is_file(), 'runtime log stays inside the installation even with external data-dir')
    check(bool(list((target / 'cache/qml').glob('*'))), 'actual QML disk cache is written inside the installation')
    old_pipeline = Path(os.environ['LOCALAPPDATA']) / 'CoRig/CoRig/cache'
    check(not any(p.stat().st_mtime > process_start for p in old_pipeline.rglob('*') if p.is_file()),
          'rendering does not write a pipeline cache into the old AppData location')
    hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in external.rglob('*') if p.is_file()}
    unrelated = target / 'docs/personal.txt'
    unrelated.write_text('User-owned document\n', encoding='utf-8')
    other_project = target / 'project/personal.txt'
    other_plugin = target / 'plugins/personal.txt'
    other_project.write_bytes(b'unrelated project-folder note')
    other_plugin.write_bytes(b'unrelated plugin-folder note')
    shutil.copy2(external / 'project/test.crpj', target / 'project/managed.crpj')
    shutil.copy2(external / 'capture.corigrec', target / 'project/managed.corigrec')
    linked = subprocess.run(['cmd.exe', '/d', '/c', 'mklink', '/J', str(target / 'external-link'), str(external)], capture_output=True)
    check(linked.returncode == 0, 'private fixture contains a real junction to external user data')
    result = uninstall(target, 'all')
    check(result['success'] and not (target / 'CoRig-uninstall.exe').exists(), 'All removes the actual running uninstaller after it exits')
    check(not unrelated.exists() and not other_project.exists() and not other_plugin.exists(),
          'All deletes manually added files in program, project and plugin directories')
    check(not target.exists(), 'All clears and removes the entire installation, including the junction itself')
    check(all(p.is_file() and hashlib.sha256(p.read_bytes()).hexdigest() == h for p, h in hashes.items()), 'All preserves every external project, setting and recording byte')

    target = clone('complete-all')
    result = uninstall(target, 'all')
    check(result['success'] and not target.exists(), 'All removes an empty installation directory including its cache and helper acknowledgement')

    target = clone('keep-data')
    (target / 'project/own.crpj').write_bytes(b'user project')
    (target / 'plugins/own.crpl').write_bytes(b'user plugin')
    (target / 'settings.ini').write_bytes(b'[user]\nvalue=1\n')
    (target / 'docs/personal.txt').write_bytes(b'unrelated document')
    (target / 'project/personal.txt').write_bytes(b'unrelated note')
    result = uninstall(target, 'keep')
    check(result['success'] and not (target / 'CoRig-uninstall.exe').exists(), 'Keep also removes the uninstaller itself')
    check((target / 'project/own.crpj').read_bytes() == b'user project' and (target / 'plugins/own.crpl').read_bytes() == b'user plugin'
          and (target / 'settings.ini').read_bytes() == b'[user]\nvalue=1\n', 'Keep preserves user projects, imported plugins and settings')
    check(not (target / 'cache').exists(), 'Keep removes disposable runtime caches')
    check((target / 'docs/personal.txt').read_bytes() == b'unrelated document' and (target / 'project/personal.txt').read_bytes() == b'unrelated note',
          'Keep preserves additional user files inside program and data folders')

    target = clone('locked-file')
    # Holding a real shipped file without FILE_SHARE_DELETE reproduces a real
    # Windows deletion failure, rather than replacing the deletion operation.
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.restype = ctypes.c_void_p
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel.CreateFileW(str(target / 'README.md'), 0x80000000, 1, None, 3, 0, None)
    if handle == ctypes.c_void_p(-1).value: raise ctypes.WinError(ctypes.get_last_error())
    try:
        result = uninstall(target, 'all')
        check(not result['success'] and result['failures'] and (target / 'README.md').exists(), 'locked program file is reported as a failed deletion, never success')
    finally:
        kernel.CloseHandle(handle)
    report = {'checks': checks, 'count': len(checks), 'run': str(run), 'hardware_used': False}
    (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'passed': len(checks), 'report': str(output / 'report.json')}), flush=True)


if __name__ == '__main__':
    main()
