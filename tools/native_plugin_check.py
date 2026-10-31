"""Validate real C++/Python packages, embedded projects and AI/GUI parity.

Use only a dedicated data directory below --output. The C++ package must have
been built from the standalone public SDK example, not application sources.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
from zipfile import ZipFile, ZIP_DEFLATED

from native_ui_check import Client


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--plugin', type=Path, required=True)
    parser.add_argument('--python', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--qt-prefix', type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    run = output / time.strftime('run-%Y%m%d-%H%M%S')
    run.mkdir()
    data = run / 'data'
    data.mkdir()
    environment = os.environ.copy()
    system = Path(os.environ['SystemRoot'])
    paths = [str(system / 'System32'), str(system)]
    if args.qt_prefix:
        paths.insert(0, str(args.qt_prefix.resolve() / 'bin'))
    environment['PATH'] = os.pathsep.join(paths)
    for name in ('PYTHONHOME', 'PYTHONPATH', 'QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'QT_QPA_PLATFORM'):
        environment.pop(name, None)
    checks = []
    process = None
    log = (run / 'process.log').open('wb')

    def check(condition, label):
        if not condition:
            raise AssertionError(label)
        checks.append(label)
        print('PASS', label, flush=True)

    def launch(project=None):
        nonlocal process
        command = [str(args.executable.resolve()), '--data-dir', str(data)]
        if project:
            command.append(str(project))
        record = data / '.sessions/api.json'
        if record.exists():
            record.unlink()
        process = subprocess.Popen(command, env=environment, stdout=log, stderr=log,
                                   creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError('CoRig exited during startup; inspect ' + str(run / 'process.log'))
            if record.exists():
                try:
                    client = Client(data)
                    client.call('app.state')
                    return client
                except (OSError, ValueError, RuntimeError):
                    pass
            time.sleep(.05)
        raise TimeoutError('CoRig API startup')

    def close(client):
        client.call('app.quit', {'discard': True})
        process.wait(timeout=10)

    def wait_output(client, node, expected):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            actual = client.call('node.state', {'node': node}).get('outputs', {}).get('out')
            if actual == expected:
                return
            time.sleep(.03)
        raise AssertionError(f'{node}: expected {expected}, got {actual}')

    def expect_error(client, operation, arguments, fragment):
        try:
            client.call(operation, arguments)
        except RuntimeError as error:
            if fragment not in str(error):
                raise
        else:
            raise AssertionError(operation + ' should have rejected this operation')

    def graph(client, plugin, name, value=3):
        client.call('project.new', {'name': name, 'discard': True})
        client.call('batch', {'commands': [
            {'op': 'graph.node.add', 'args': {'id': 'input', 'plugin': 'control.number', 'config': {'value': value}, 'position': [32, 48]}},
            {'op': 'graph.node.add', 'args': {'id': 'gain', 'plugin': plugin, 'position': [360, 48]}},
            {'op': 'graph.wire.add', 'args': {'id': 'signal', 'source': ['input', 'value'], 'target': ['gain', 'x']}}
        ]})

    def python_package(version, gain):
        path = run / ('PythonGain-' + version + '.crpl')
        manifest = {
            'format': 'corig-plugin', 'package_api': 1, 'id': 'user.example.python_gain',
            'name': 'Python 增益验证', 'version': version, 'implementation': 'python',
            'plugins': [{'id': 'user.example.python_gain', 'name': 'Python 增益验证', 'code_file': 'gain.py',
                         'ports': [{'id': 'x', 'direction': 'in', 'data_type': 'number'},
                                   {'id': 'out', 'direction': 'out', 'data_type': 'number'}]}]
        }
        with ZipFile(path, 'w', ZIP_DEFLATED) as archive:
            archive.writestr('plugin.json', json.dumps(manifest, ensure_ascii=False))
            archive.writestr('gain.py', f"def process(inputs, state):\n    return {{'out': inputs['x'] * {gain}}}\n")
        return path

    try:
        client = launch()
        state = client.call('app.state')
        check(state['application'] == 'CoRig' and state['version'] == '0.4.1', 'AI connects to the actual CoRig 0.4 application')
        required = {'plugin.state', 'plugin.import', 'plugin.enable', 'plugin.remove', 'plugin.export', 'update.check', 'update.install', 'app.restart'}
        check(required <= set(client.call('schema')['commands']), 'AI discovery exposes the new plugin and installation commands')
        official = client.call('registry')
        check(len(official) == 78 and all(p['implementation'] == 'native' and not p['builtin'] for p in official), 'all 78 shipped functional nodes are external native plugins')
        check(not state['python'], 'official functionality starts without a selected Python interpreter')
        client.call('plugin.settings')
        client.call('controls.click', {'id':'plugins.collapse_all'})
        client.call('controls.set', {'id':'plugins.search','property':'text','value':'control.number'})
        time.sleep(.15)
        client.call('controls.click', {'id':'plugins.tree.package.org.corig.control.number'})
        check(client.call('controls.get', {'id':'plugins.enabled.package.org.corig.control.number'})['checked'], 'local manager search reveals a package through collapsed folders')
        client.call('controls.click', {'id':'plugins.enabled.package.org.corig.control.number'})
        check(not any(p['id']=='control.number' for p in client.call('registry')), 'tree checkbox changes the actual local enable preference')
        local_preview = client.call('library.preview', {'plugin':'control.number','scope':'library'})
        check(local_preview['definition']['id']=='control.number' and local_preview['geometry']['ports'] and local_preview['view_urls'], 'disabled local plugin retains its real geometry, declaration and QML preview')
        check(client.call('controls.get', {'id':'plugins.description.control.number'})['text']==local_preview['definition']['description'], 'right detail uses the installed plugin description')
        client.call('controls.click', {'id':'plugins.enabled.folder.控件'})
        check(all(p['enabled'] for p in client.call('plugin.state')['plugins'] if p['package'].startswith('org.corig.control.')), 'partly enabled category can enable all its plugins')
        client.call('controls.click', {'id':'plugins.enabled.folder.控件'})
        check(not any(p['enabled'] for p in client.call('plugin.state')['plugins'] if p['package'].startswith('org.corig.control.')), 'category uncheck disables hidden descendants despite the active search')
        client.call('controls.click', {'id':'plugins.enabled.folder.控件'})
        check(all(p['enabled'] for p in client.call('plugin.state')['plugins'] if p['package'].startswith('org.corig.control.')), 'category check restores its complete subtree')
        client.call('controls.set', {'id':'plugins.search','property':'text','value':''})
        client.call('controls.click', {'id':'plugins.expand_all'})
        client.call('controls.click', {'id':'plugins.collapse_all'})
        client.call('controls.click', {'id':'plugins.tree.toggle.控件'})
        time.sleep(.1)
        client.call('controls.click', {'id':'plugins.tree.package.org.corig.control.number'})
        client.call('capture', {'target':'window','path':str(run/'local-manager-tree.png')})
        client.call('controls.click', {'id':'plugins.close'})

        client.call('plugin.import', {'path': str(args.plugin.resolve())})
        check(any(p['id'] == 'user.example.native_gain' for p in client.call('registry')), 'a standalone SDK build imports through the shared plugin command')
        graph(client, 'user.example.native_gain', '独立 C++ 插件验收')
        client.call('ui.view.add', {'id': 'native_view', 'parent': 'root', 'node': 'gain', 'view': 'result'})
        client.call('ui.item.patch', {'id': 'native_view', 'properties': {'x': 32, 'y': 32, 'width': 400, 'height': 160}})
        client.call('workspace.open', {'page': 'runtime'})
        client.call('runtime.start')
        wait_output(client, 'gain', 6)
        check(True, 'external C++ code computes actual upstream values')
        client.call('node.action', {'node': 'input', 'action': 'set_value', 'args': {'value': 7}})
        wait_output(client, 'gain', 14)
        controls = client.call('controls.list')
        check(any(c['id'] == 'native_gain.reset' and c.get('node') == 'gain' for c in controls), 'AI discovers controls and ownership inside a C++ plugin QML view')
        client.call('controls.click', {'id': 'native_gain.reset'})
        wait_output(client, 'gain', 0)
        check(True, 'AI control activation reaches the real C++ action and output')
        client.call('runtime.pause')
        time.sleep(.15)
        client.call('capture', {'target': 'runtime', 'path': str(run / 'native-view.png')})
        export_tree=client.call('plugin.tree',{'scope':'project'})
        paths={p['id']:p['relative_path'] for p in export_tree['plugins']}
        check(paths['org.corig.control.number'].startswith('控件/'), 'AI plugin tree derives the official category from the actual package folder')
        selected=['org.corig.control.number','user.example.native_gain']
        tree_output=run/'tree-export'
        exported_tree=client.call('plugin.export',{'plugins':selected,'directory':str(tree_output)})
        check(exported_tree['count']==2 and all((tree_output/paths[p]).is_file() for p in selected), 'selected batch export preserves original relative folder structure')
        existing=client.call('plugin.export',{'plugins':selected,'directory':str(tree_output)})
        check(existing['requires_confirmation'] and len(existing['conflicts'])==2, 'batch export lists existing targets before overwrite')
        client.call('plugin.export.dialog',{'scope':'project'})
        client.call('controls.click',{'id':'plugin_export.all'})
        check(client.call('controls.get',{'id':'plugin_export.check.user.example.native_gain'})['checked'], 'tree selection supports select all')
        client.call('controls.click',{'id':'plugin_export.invert'})
        check(not client.call('controls.get',{'id':'plugin_export.check.user.example.native_gain'})['checked'], 'tree selection supports invert')
        client.call('controls.set',{'id':'plugin_export.folder.控件','property':'checked','value':True})
        check(client.call('controls.get',{'id':'plugin_export.check.org.corig.control.number'})['checked'] and not client.call('controls.get',{'id':'plugin_export.check.user.example.native_gain'})['checked'], 'checking a category selects only that subtree')
        client.call('capture',{'target':'window','path':str(run/'export-tree.png')})
        client.call('controls.click',{'id':'plugin_export.close'})
        client.call('project.options',{'include_plugins':False})
        check(client.call('project.options')['include_plugins'] is False and client.call('app.state')['dirty'], 'AI can change the persistent project plugin setting and mark the project dirty')
        light=client.call('project.export',{'path':str(run/'DependenciesOnly.crpj')})
        check(light['include_plugins'] is False and len(light['dependencies'])==2, 'project export can record dependencies without embedding packages')
        client.call('project.options',{'include_plugins':True})
        project = run / 'NativeGain.crpj'
        client.call('project.save', {'path': str(project)})
        embedded = client.call('project.get')['plugin_packages']
        check({p['id'] for p in embedded} == {'org.corig.control.number', 'user.example.native_gain'}, 'project embeds both its official and user C++ plugin packages')
        exported = run / 'NativeGain-exported.crpl'
        official_exported = run / 'Official-exported.crpl'
        client.call('plugin.export', {'plugin': 'user.example.native_gain', 'path': str(exported)})
        client.call('plugin.export', {'plugin': 'control.number', 'path': str(official_exported)})
        check(exported.read_bytes() == args.plugin.read_bytes(), 'plugin export preserves the actual imported package bytes')
        expect_error(client, 'plugin.enable', {'plugin': 'user.example.native_gain', 'enabled': False}, '使用')
        expect_error(client, 'plugin.remove', {'package': 'org.corig.control.number'}, '使用')
        check(True, 'in-use plugin disabling and removal report the affected references')
        client.call('plugin.enable', {'plugin': 'function.cos', 'enabled': False})
        check(not any(p['id'] == 'function.cos' for p in client.call('registry')), 'unused official plugins can be disabled individually')
        client.call('plugin.enable', {'plugin': 'function.cos', 'enabled': True})
        client.call('runtime.stop')
        client.call('project.new', {'discard': True})
        client.call('plugin.remove', {'package': 'org.corig.function.cos'})
        close(client)
        original=data/'plugins'/'NativeGain.crpl'
        moved=data/'plugins'/'用户'/'机器人'/'NativeGain.crpl'
        moved.parent.mkdir(parents=True,exist_ok=True)
        original.rename(moved)
        client=launch(project)
        deadline=time.monotonic()+10
        while not client.call('project.get')['nodes'] and time.monotonic()<deadline:time.sleep(.05)
        imported=next(p for p in client.call('registry') if p['id']=='user.example.native_gain')
        check(imported['category_path']==['用户','机器人'], 'moving a package changes classification after restart without changing its contents')
        client.call('project.save',{'path':str(project)})
        client.call('project.new',{'discard':True})
        client.call('plugin.remove', {'package': 'user.example.native_gain'})
        check(not any(p['id']=='function.cos' for p in client.call('registry')), 'official nodes are independent removable packages')
        close(client)
        # Only this harness's private library is removed, while the app is closed.
        library=data/'plugins'
        for package_file in library.rglob('*.crpl'):
            assert package_file.resolve().is_relative_to(library.resolve())
            package_file.unlink()
        client = launch(project)
        deadline = time.monotonic() + 10
        while not client.call('project.get')['nodes'] and time.monotonic() < deadline:
            time.sleep(.05)
        client.call('runtime.start')
        wait_output(client, 'gain', 6)
        check(not client.call('app.state')['python'], 'a fresh process reopens the self-contained C++ project without Python or installed packages')
        check(len(client.call('registry'))==2, 'no hidden built-in nodes return when the installed library is absent')
        check(not client.call('plugin.state')['packages'] and not client.call('plugin.tree',{'scope':'library'})['plugins'], 'local inventory excludes project-contained packages when the local folder is empty')
        check(len(client.call('plugin.tree',{'scope':'project'})['plugins'])==2, 'embedded dependencies remain in the separate project inventory')
        client.call('plugin.settings')
        client.call('capture', {'target':'window','path':str(run/'empty-local-manager.png')})
        client.call('controls.click', {'id':'plugins.close'})

        embedded_export=run/'Embedded-exported.crpl'
        client.call('plugin.export', {'plugin':'user.example.native_gain','path':str(embedded_export)})
        check(embedded_export.read_bytes()==exported.read_bytes(), 'a project-contained plugin exports without a local installed copy')
        client.call('runtime.stop')
        missing=client.call('project.open',{'path':str(run/'DependenciesOnly.crpj')})
        check(missing['requires_plugins'] and len(missing['missing_plugins'])==2, 'a dependency-only project lists the actual missing packages before opening')
        client.call('controls.click',{'id':'plugins.version.cancel'})
        client.call('project.new', {'discard': True})
        client.call('plugin.import', {'path': str(official_exported)})
        client.call('preferences.set', {'python': str(args.python.resolve())})
        first, second = python_package('1.0.0', 2), python_package('2.0.0', 3)
        client.call('plugin.import', {'path': str(first)})
        graph(client, 'user.example.python_gain', 'Python 版本选择验收', 4)
        client.call('runtime.start')
        wait_output(client, 'gain', 8)
        client.call('runtime.stop')
        python_project = run / 'PythonV1.crpj'
        client.call('project.save', {'path': str(python_project)})
        client.call('project.new', {'discard': True})
        client.call('plugin.import', {'path': str(second)})
        conflict = client.call('project.open', {'path': str(python_project)})
        check(conflict['requires_plugin_selection'] and conflict['conflicts'][0]['project_version'] == '1.0.0' and conflict['conflicts'][0]['local_version'] == '2.0.0', 'AI receives both real package versions before the current project is replaced')
        time.sleep(.15)
        client.call('capture', {'target': 'window', 'path': str(run / 'version-conflict.png')})
        client.call('controls.set', {'id': 'plugins.version.user.example.python_gain', 'property': 'currentIndex', 'value': 1})
        client.call('controls.click', {'id': 'plugins.version.open'})
        client.call('runtime.start')
        wait_output(client, 'gain', 8)
        check(True, 'the actual version-selection dialog loads and runs the project-contained Python version')
        local=next(p for p in client.call('plugin.state')['packages'] if p['id']=='user.example.python_gain')
        check(local['version']=='2.0.0', 'local manager reports the local version while the project runs a different embedded version')
        local_export=run/'Python-local-version.crpl'
        client.call('plugin.export', {'plugin':'user.example.python_gain','scope':'library','path':str(local_export)})
        check(local_export.read_bytes()==second.read_bytes(), 'local-scope export uses the installed version independently of the project choice')


        client.call('runtime.stop')
        conflict = client.call('project.open', {'path': str(python_project)})
        check(conflict['requires_plugin_selection'], 'reopening a conflicting project asks for a fresh explicit choice')
        client.call('controls.set', {'id': 'plugins.version.user.example.python_gain', 'property': 'currentIndex', 'value': 2})
        client.call('controls.click', {'id': 'plugins.version.open'})
        client.call('runtime.start')
        wait_output(client, 'gain', 12)
        check(True, 'the same dialog can select and run the installed Python version')
        client.call('runtime.stop')
        chosen_export = run / 'PythonChosen.crpl'
        client.call('plugin.export', {'plugin': 'user.example.python_gain', 'path': str(chosen_export)})
        check(chosen_export.read_bytes() == second.read_bytes(), 'export follows the version actually selected for the active project')
        client.call('project.save', {'path': str(run / 'PythonChosen.crpj')})
        client.call('plugin.settings')
        client.call('controls.set', {'id':'plugins.search','property':'text','value':'control.number'})
        time.sleep(.15)
        client.call('controls.click', {'id':'plugins.tree.package.org.corig.control.number'})
        check(any(c['id'] == 'plugins.enabled.package.org.corig.control.number' for c in client.call('controls.list')), 'AI discovers the real plugin enable controls in plugin settings')
        client.call('capture', {'target': 'window', 'path': str(run / 'plugin-settings.png')})
        client.call('controls.click', {'id': 'plugins.close'})
        close(client)
        report = {'application': 'CoRig', 'version': state['version'], 'checks': checks, 'run': str(run),
                  'native_package_sha256': hashlib.sha256(args.plugin.read_bytes()).hexdigest()}
        (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), 'utf-8')
        print(json.dumps({'passed': len(checks), 'report': str(output / 'report.json')}, ensure_ascii=False))
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
        log.close()


if __name__ == '__main__':
    main()
