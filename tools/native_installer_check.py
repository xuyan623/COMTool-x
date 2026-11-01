"""Verify real IFW installation, app maintenance commands and data retention.

All installation roots and update fixtures belong to this run. Shortcuts and
file associations are disabled; personal data is kept outside the private root.
Only fixture component metadata advances beyond the built version, leaving release sources
and the production update repository unchanged.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import xml.etree.ElementTree as ET

from native_ui_check import Client


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--plugin', type=Path, help='Also exercise importing and restarting with a standalone native SDK package')
    parser.add_argument('--disable-plugin', action='store_true', help='Also exercise saved-project restart with a disabled official plugin')
    parser.add_argument('--ifw-bin', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    run = root / 'build' / 'installer-check' / time.strftime('run-%Y%m%d-%H%M%S')
    run.mkdir(parents=True)
    # Test-owned installer scratch space also stays on the workspace drive.
    scratch = run / 'scratch'
    scratch.mkdir()
    for key in ('TEMP', 'TMP', 'TMPDIR'):
        os.environ[key] = str(scratch)
    installed, data = run / 'installed', run / 'data'
    data.mkdir()
    ifw = (args.ifw_bin or root / '.cache/qt-ifw/Tools/QtInstallerFramework/4.7/bin').resolve()
    checks, covered = [], set()
    log = (run / 'process.log').open('ab')
    client = None
    environment = os.environ.copy()
    system = Path(os.environ['SystemRoot'])
    environment['PATH'] = os.pathsep.join([str(system / 'System32'), str(system)])
    for key in ('QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'PYTHONHOME', 'PYTHONPATH', 'QT_QPA_PLATFORM'):
        environment.pop(key, None)

    def check(condition, label):
        if not condition:
            raise AssertionError(label)
        checks.append(label)
        print('PASS', label, flush=True)

    def command(arguments):
        result = subprocess.run([str(a) for a in arguments], stdout=log, stderr=log, timeout=180)
        if result.returncode:
            raise RuntimeError('Installer command failed; inspect ' + str(run / 'process.log'))

    def api(operation, arguments=None):
        result = client.call(operation, arguments)
        covered.add(operation)
        return result

    def connect(excluded_pid=0):
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            try:
                record = json.loads((data / '.sessions/api.json').read_text('utf-8'))
                if record['pid'] != excluded_pid:
                    candidate = Client(data)
                    candidate.call('app.state')
                    return candidate, record['pid']
            except (OSError, ValueError, RuntimeError):
                pass
            time.sleep(.05)
        raise TimeoutError('Installed app discovery')

    def launch(project=None):
        arguments = [str(installed / 'CoRig.exe'), '--data-dir', str(data)]
        if project:
            arguments.append(str(project))
        process = subprocess.Popen(arguments, env=environment, stdout=log, stderr=log,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        connection, pid = connect()
        if pid != process.pid:
            raise RuntimeError('Unexpected installed application instance')
        if project:
            deadline = time.monotonic() + 60
            while time.monotonic() < deadline:
                state = connection.call('app.state')
                if not state['busy'] and state['path'] and Path(state['path']).resolve() == project.resolve():
                    break
                time.sleep(.05)
            else:
                process.terminate()
                process.wait(timeout=10)
                raise TimeoutError('Installed project open did not complete')
        return connection, process

    def close_maintenance():
        # Locate only the maintenance executable created in this private run.
        # This closes its launch-verification window before the real CLI update.
        target = str(installed / 'CoRigMaintenanceTool.exe').replace("'", "''")
        query = ("Get-CimInstance Win32_Process -Filter \"Name='CoRigMaintenanceTool.exe'\" | "
                 "Where-Object {$_.ExecutablePath -eq '" + target + "'} | Select-Object -ExpandProperty ProcessId")
        result = subprocess.run(['powershell.exe', '-NoProfile', '-Command', query],
                                capture_output=True, text=True, timeout=15)
        pids = [int(p) for p in result.stdout.split()]
        for pid in pids:
            closed = subprocess.run(['taskkill.exe', '/PID', str(pid)], stdout=log, stderr=log)
            if closed.returncode:
                subprocess.run(['taskkill.exe', '/F', '/PID', str(pid)], stdout=log, stderr=log, check=True)
        return pids

    try:
        # BEGIN isolated update fixture: the version marker is test metadata,
        # never a change to the production program version or published feed.
        fixture = run / 'update-packages/org.corig.application'
        shutil.copytree(root / 'build/installer/packages/org.corig.application', fixture)
        metadata = fixture / 'meta/package.xml'
        xml = ET.parse(metadata)
        release_version = xml.find('Version').text
        fixture_version = release_version + '.1'
        xml.find('Version').text = fixture_version
        xml.write(metadata, encoding='utf-8', xml_declaration=True)
        (fixture / 'data/update-proof.txt').write_text('Real private component update.\n', encoding='utf-8')
        manifest = fixture / 'data/install-manifest.json'
        ownership = json.loads(manifest.read_text('utf-8-sig'))
        ownership['files'].append('update-proof.txt')
        ownership['files'] = [name.replace('installerResources/org.corig.application/' + release_version,
                                          'installerResources/org.corig.application/' + fixture_version)
                              for name in ownership['files']]
        manifest.write_text(json.dumps(ownership), encoding='utf-8')
        repository = run / 'update-repository'
        command([ifw / 'repogen.exe', '-p', fixture.parent, repository])
        config = run / 'config.xml'
        template = (root / 'build/installer/config/config.xml').read_text('utf-8-sig')
        config_xml = ET.fromstring(template)
        config_xml.find('RemoteRepositories/Repository/Url').text = repository.as_uri()
        ET.ElementTree(config_xml).write(config, encoding='utf-8', xml_declaration=True)
        setup = run / 'CoRigSetup-check.exe'
        command([ifw / 'binarycreator.exe', '-c', config, '-p', root / 'build/installer/packages', setup])
        # END fixture. Purging the private install below retains its sibling
        # data directory as evidence and removes only this test installation.
        command([setup, '--cache-path', run / 'installation-cache', '--root', installed, '--set-temp-repository', (root / 'dist/updates/windows-x64').as_uri(),
                 '--accept-licenses', '--accept-messages', '--confirm-command', 'install',
                 'org.corig.application', 'org.corig.official', 'org.corig.sdk',
                 'AllUsers=false', 'CreateShortcuts=false', 'RegisterFileTypes=false'])
        tool = installed / 'CoRigMaintenanceTool.exe'
        network_cache = ET.parse(installed / 'network.xml').findtext('LocalCachePath')
        check(Path(network_cache).resolve() == (installed / 'cache/installer').resolve(),
              'installation configures maintenance metadata cache inside its own directory before first launch')
        check(tool.is_file() and (installed / 'CoRigSdk/include/corig/plugin.h').is_file(),
              'real current-user installation contains the maintenance tool and optional public SDK')
        check(not (installed / 'portable.ini').exists(), 'installation excludes portable-mode settings')
        setup.unlink()
        check(not setup.exists(), 'original setup executable is removed before app launch, update and uninstall verification')
        qt_hash = hashlib.sha256((installed / 'Qt6Core.dll').read_bytes()).hexdigest()
        client, process = launch()
        check(api('update.state')['supported'], 'installed app discovers the real IFW maintenance executable')
        check(len(api('registry')) == 78 and not api('app.state')['python'],
              'installed application runs all official declarations with no developer Qt or Python paths')
        api('preferences.set', {'theme': 'light'})
        if args.disable_plugin:
            api('plugin.enable', {'plugin': 'function.cos', 'enabled': False})
        if args.plugin:
            api('plugin.import', {'path': str(args.plugin.resolve())})
        api('graph.node.add', {'id': 'input', 'plugin': 'control.number', 'config': {'value': 9}})
        project = data / 'project/Preserved.crpj'
        api('project.save', {'path': str(project)})
        personal = data / 'personal-note.txt'
        personal.write_text('User-owned data outside installation root.\n', encoding='utf-8')
        changed = api('update.check')
        check(changed['available'] and changed['updates'][0]['version'] == fixture_version,
              'app update.check parses an actual available component from its private IFW repository')
        old_pid = process.pid
        api('app.restart')
        process.wait(timeout=10)
        client, restarted_pid = connect(old_pid)
        deadline = time.monotonic() + 30
        while (not api('app.state')['path'] or api('app.state')['busy']) and time.monotonic() < deadline:
            time.sleep(.05)
        check(restarted_pid != old_pid and Path(api('app.state')['path']).resolve() == project.resolve(),
              'app.restart starts a new process with the same data directory and saved project')
        api('app.quit')
        time.sleep(.4)
        preserved = [project, personal]
        if args.plugin:
            original = data / 'plugins/NativeGain.crpl'
            moved = data / 'plugins/用户/验证/NativeGain.crpl'
            moved.parent.mkdir(parents=True)
            original.rename(moved)
            preserved.append(moved)
        hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in preserved}
        client, process = launch(project)
        api('update.install')
        process.wait(timeout=10)
        time.sleep(1)
        check(bool(close_maintenance()), 'app update.install launches the real graphical updater and closes the saved application')
        client = None
        command([tool, '--cache-path', installed / 'cache/installer', '--accept-licenses', '--accept-messages', '--confirm-command', 'update', 'org.corig.application'])
        check((installed / 'update-proof.txt').is_file(), 'the actual maintenance update installs new application payload')
        check(hashlib.sha256((installed / 'Qt6Core.dll').read_bytes()).hexdigest() == qt_hash,
              'updating the application retains the unchanged Qt runtime component')
        check(all(hashlib.sha256(p.read_bytes()).hexdigest() == h for p, h in hashes.items()),
              'update preserves all selected project, personal file and optional plugin bytes')
        client, process = launch(project)
        check(api('app.state')['theme'] == 'light' and next(p for p in api('plugin.state')['plugins'] if p['id'] == 'function.cos')['enabled'] != args.disable_plugin,
              'theme and the selected plugin enablement setting survive the update')
        if args.plugin:
            check(next(p for p in api('plugin.state')['packages'] if p['id'] == 'user.example.native_gain')['relative_path'] == '用户/验证/NativeGain.crpl',
                  'the updated application retains the user-defined plugin hierarchy')
        check(not api('update.check')['available'], 'the updated installation reports no further component update')
        api('app.quit')
        process.wait(timeout=10)
        client = None
        # Verify the private root before invoking the real scoped uninstaller.
        assert installed.resolve().parent == run.resolve() and not data.resolve().is_relative_to(installed.resolve())
        cleanup_report = run / 'uninstall-result.json'
        command([installed / 'CoRig-uninstall.exe', '--silent', 'all', '--report', cleanup_report])
        deadline = time.monotonic() + 60
        while not cleanup_report.exists() and time.monotonic() < deadline:
            time.sleep(.1)
        cleanup = json.loads(cleanup_report.read_text('utf-8-sig'))
        check(cleanup['success'] and not installed.exists() and all(p.is_file() for p in hashes), 'private uninstall removes itself, SDK and the complete installation while preserving external user data')
        report = {'application': 'CoRig', 'version': release_version, 'checks': checks, 'count': len(checks),
                  'covered_commands': sorted(covered), 'run': str(run), 'public_repository_used': False,
                  'native_sdk_plugin_tested': bool(args.plugin),
                  'disabled_plugin_restart_tested': args.disable_plugin,
                  'fixture_component_version': fixture_version, 'all_users_elevation_tested': False}
        (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), 'utf-8')
        print(json.dumps({'passed': len(checks), 'report': str(output / 'report.json')}, ensure_ascii=False))
    finally:
        if client is not None:
            try:
                client.call('app.quit', {'discard': True})
            except (OSError, RuntimeError):
                pass
        log.close()


if __name__ == '__main__':
    main()
