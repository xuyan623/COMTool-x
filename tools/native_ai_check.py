"""Verify every public command and the AI design workflow through real HTTP.

The dedicated application instance, Python extension, controls and numerical
results are real. No desktop automation or hardware connection is involved.
"""
from __future__ import annotations

import argparse
import json
import math
import subprocess
import sys
import time
from pathlib import Path

from native_ui_check import Client


class CoverageClient(Client):
    """Record successful public operations, including completed child jobs."""

    def __init__(self, directory):
        super().__init__(directory)
        self.covered = set()

    def call(self, operation, arguments=None, *, wait=True):
        result = super().call(operation, arguments, wait=wait)
        self.covered.add(operation)
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--python', type=Path, default=Path(sys.executable))
    args = parser.parse_args()
    data, output = args.data_dir.resolve(), args.output.resolve()
    executable = args.executable.resolve()
    if (data / '.sessions/api.json').exists():
        raise RuntimeError('Use a dedicated data directory with no running instance')
    data.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    process = subprocess.Popen([str(executable), '--data-dir', str(data)])
    client, checks = None, []

    def check(condition, message):
        if not condition:
            raise AssertionError(message)
        checks.append(message)
        print('PASS', message, flush=True)

    def reject(operation, arguments):
        try:
            client.call(operation, arguments)
        except RuntimeError:
            return
        raise AssertionError('Invalid operation was accepted: ' + operation)

    def wait_state(node, predicate):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            state = client.call('node.state', {'node': node})
            if predicate(state):
                return state
            time.sleep(.02)
        raise AssertionError('Node did not reach its expected state: ' + node)

    try:
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError('Test application exited during startup')
            try:
                record = json.loads((data / '.sessions/api.json').read_text('utf-8'))
                if record['pid'] == process.pid:
                    client = CoverageClient(data)
                    client.call('app.state')
                    break
            except (OSError, ValueError, RuntimeError):
                pass
            time.sleep(.05)
        else:
            raise TimeoutError('Test application discovery')
        schema = client.call('schema')
        registry = {p['id']: p for p in client.call('registry')}
        (output / 'schema.json').write_text(json.dumps(schema, ensure_ascii=False, indent=2), 'utf-8')
        client.call('io.ports')
        client.call('preferences.set', {'theme': 'light', 'read_only': False, 'python': str(args.python.resolve())})
        client.call('preferences.set', {'theme': 'dark'})
        client.call('python.probe', {'dependencies': ['math', 'json']})
        client.call('library.state')
        client.call('library.search', {'query': '曲线', 'scope': 'all'})
        client.call('library.preview', {'plugin': 'plot.curves'})
        client.call('library.favorite', {'plugin': 'plot.curves', 'enabled': True})
        client.call('library.configure', {'view': 'list', 'size': 1.2})
        client.call('library.configure', {'view': 'grid', 'size': 1.0})
        check(len(registry) == 78, 'plugins, properties, actions and UI declarations are discoverable')

        client.call('project.new', {'name': 'AI 协作完整流程'})
        client.call('project.rename', {'name': 'AI 协作验收'})
        for ident, plugin, position in [('input', 'control.number', [40, 40]), ('alternate', 'control.number', [40, 300]),
                                         ('math', 'function.exp', [400, 40]), ('result', 'display.value', [800, 40])]:
            client.call('graph.node.add', {'id': ident, 'plugin': plugin, 'position': position})
        client.call('batch', {'commands': [
            {'op': 'graph.node.patch', 'args': {'id': 'input', 'changes': {'title': '批量配置的输入'}}},
            {'op': 'graph.node.move', 'args': {'id': 'alternate', 'position': [60, 320]}}
        ]})
        client.call('graph.node.move', {'id': 'alternate', 'position': [40, 340]})
        client.call('graph.node.patch', {'id': 'math', 'changes': {'title': '实际指数运算', 'size': [300, 180]}})
        client.call('graph.node.rotate', {'id': 'math', 'degrees': 90})
        client.call('graph.node.rotate', {'id': 'math', 'degrees': 270})
        client.call('graph.select', {'id': 'math', 'ids': ['math']})
        client.call('graph.fit')
        client.call('graph.overlaps')
        options = client.call('graph.connection.options', {'node': 'input', 'port': 'value'})
        check(options['math/x']['compatible'], 'AI receives the same connection compatibility as the editor')
        client.call('graph.wire.add', {'id': 'compute', 'source': ['input', 'value'], 'target': ['math', 'x']})
        client.call('graph.wire.patch', {'id': 'compute', 'changes': {'routing': 'polyline', 'points': [[330, 150]], 'color': '#66B4F4', 'width': 2}})
        client.call('graph.wire.handles', {'id': 'compute', 'x': 330, 'y': 150})
        client.call('graph.wire.add', {'id': 'observe', 'source': ['math', 'out'], 'target': ['result', 'value']})
        client.call('graph.wire.remove', {'id': 'observe'})
        client.call('graph.wire.add', {'id': 'observe', 'source': ['math', 'out'], 'target': ['result', 'value']})

        client.call('editor.add', {'id': 'spare', 'page': 'graph', 'plugin': 'function.sin'})
        client.call('editor.place', {'id': 'spare', 'page': 'graph', 'position': [600, 360]})
        client.call('editor.reorder', {'id': 'spare', 'page': 'graph', 'index': 0})
        plan = client.call('editor.plan', {'page': 'graph', 'ids': ['spare'], 'operation': 'replace', 'plugin': 'function.cos'})
        client.call('editor.replace', {'page': 'graph', 'ids': ['spare'], 'plugin': 'function.cos', 'confirmed': True, 'revision': plan['revision']})
        client.call('editor.copy', {'page': 'graph', 'ids': ['spare']})
        payload = json.loads(client.call('clipboard.read')['text'])
        before = {n['id'] for n in client.call('project.get')['nodes']}
        client.call('editor.paste', {'page': 'graph', 'payload': payload})
        copied = next(n['id'] for n in client.call('project.get')['nodes'] if n['id'] not in before)
        client.call('editor.withdraw', {'page': 'graph', 'id': copied})
        plan = client.call('editor.plan', {'page': 'graph', 'ids': [copied], 'operation': 'remove'})
        client.call('editor.remove', {'page': 'graph', 'ids': [copied], 'confirmed': True, 'revision': plan['revision']})
        client.call('graph.node.remove', {'id': 'spare'})
        client.call('node.configure', {'node': 'input', 'changes': {'value': 1.25}})
        client.call('history.undo')
        client.call('history.redo')

        plan = client.call('sync.plan', {'direction': 'graph_to_ui'})
        client.call('sync.apply', {'direction': 'graph_to_ui', 'revision': plan['revision'], 'keys': [c['key'] for c in plan['changes']]})
        client.call('ui.update')
        views = {i['node']: i for i in client.call('project.get')['ui']['items'] if 'node' in i}
        client.call('ui.item.schema', {'id': views['input']['id']})
        client.call('ui.container.add', {'id': 'row', 'parent': 'root', 'plugin': 'layout.row', 'properties': {'x': 16, 'y': 16, 'width': 720, 'height': 160}})
        for ident in ('input', 'result'):
            client.call('editor.place', {'page': 'layout', 'id': views[ident]['id'], 'parent': 'row'})
        client.call('ui.item.patch', {'id': views['input']['id'], 'properties': {'font_size': 20, 'title': '数值输入'}})
        extra_view = next(iter(registry['control.number']['views']))
        client.call('ui.view.add', {'id': 'extra', 'node': 'input', 'view': extra_view, 'parent': 'root'})
        client.call('ui.item.move', {'id': 'extra', 'parent': 'row', 'index': 0})
        client.call('ui.view.bind', {'id': 'extra', 'node': 'alternate', 'view': extra_view})
        rebound = next(i for i in client.call('project.get')['ui']['items'] if i['id'] == 'extra')
        check(rebound['node'] == 'alternate', 'view binding changes preserve the item ID and layout')
        client.call('ui.item.remove', {'id': 'extra'})
        client.call('sync.plan', {'direction': 'ui_to_graph'})

        for page in ('graph', 'layout'):
            client.call('workspace.open', {'page': page})
            client.call('editor.configure', {'page': page, 'zoom': .8, 'pan_x': 145, 'pan_y': -23, 'left_width': 176, 'right_width': 240})
            time.sleep(.08)
            state = client.call('editor.state', {'page': page})
            check(state['zoom'] == .8 and state['pan_x'] == 145 and state['left_width'] == 176 and state['right_width'] == 240,
                  page + ' viewport and sidebars round-trip through the public API')
            reject('editor.configure', {'page': page, 'zoom': 0, 'pan_x': 999})
            check(client.call('editor.state', {'page': page}) == state, page + ' rejects invalid updates without partial changes')
            client.call('editor.configure', {'page': page, 'left_width': 0, 'right_width': 0})
            time.sleep(.06)
            check(client.call('editor.state', {'page': page})['left_width'] == 0, page + ' sidebars can be completely collapsed')
            client.call('editor.configure', {'page': page, 'left_width': 220, 'right_width': 306, 'fit': True})
        client.call('editor.configure', {'page': 'graph', 'animation': False})
        check(not client.call('editor.state', {'page': 'graph'})['animation'], 'API controls the same animation setting as the checkbox')

        main_window = next(w['id'] for w in client.call('app.state')['workspaces']['windows'] if not w['child'])
        client.call('window.configure', {'window': main_window, 'width': 1280, 'height': 800, 'state': 'normal'})
        client.call('workspace.open', {'page': 'logs'})
        child = client.call('workspace.detach', {'page': 'logs', 'x': 120, 'y': 100})['window']
        client.call('window.pin', {'window': child, 'enabled': True})
        client.call('window.pin', {'window': child, 'enabled': False})
        client.call('window.configure', {'window': child, 'x': 180, 'y': 120, 'width': 900, 'height': 650, 'state': 'minimized'})
        restored = client.call('window.configure', {'window': child, 'state': 'normal', 'focus': True})
        check(restored['state'] == 'normal', 'AI restores a minimized native window without desktop automation')
        client.call('workspace.move', {'page': 'logs', 'window': main_window, 'index': 0})
        child = client.call('workspace.detach', {'page': 'layout', 'x': 180, 'y': 120})['window']
        client.call('window.close', {'window': child})
        client.call('workspace.open', {'page': 'layout'})
        client.call('workspace.close', {'page': 'logs'})

        client.call('workspace.open', {'page': 'runtime'})
        client.call('runtime.start', {'debug': False})
        time.sleep(.15)
        client.call('controls.list', {'page': 'runtime'})
        client.call('controls.focus', {'id': 'control.input', 'page': 'runtime'})
        client.call('controls.set', {'id': 'control.input', 'page': 'runtime', 'property': 'text', 'value': '1.25'})
        client.call('controls.commit', {'id': 'control.input', 'page': 'runtime'})
        state = wait_state('result', lambda s: abs(s.get('inputs', {}).get('value', 0)-math.exp(1.25)) < 1e-10)
        check(abs(state['inputs']['value']-math.exp(1.25)) < 1e-10, 'AI control editing propagates a real numerical result')
        client.call('controls.get', {'id': 'control.input', 'page': 'runtime'})
        client.call('runtime.pause')
        client.call('runtime.debug', {'enabled': True})
        client.call('runtime.emit', {'node': 'input', 'outputs': {'value': 2}})
        client.call('runtime.state')
        client.call('log.configure', {'detail': True})
        client.call('log.query', {'limit': 20})
        client.call('log.export', {'path': str(output / 'design-log.jsonl')})
        client.call('log.clear')
        client.call('runtime.stop')
        client.call('runtime.reset')
        check(client.call('project.validate')['valid'], 'edited project validates before saving')
        saved = output / 'AiDesign.corig'
        client.call('project.save', {'path': str(saved)})
        project = client.call('project.get')
        client.call('project.open', {'path': str(saved)})
        check(client.call('project.get') == project, 'AI-designed objects, bindings and geometry survive reopening')

        # A real extension deliberately omits objectName on every QML control.
        # Its actions modify Python state and publish computed output values.
        code = '''def process(inputs, state):
    value = inputs.get('x', 0) * inputs['__config__']['gain']
    state['last'] = value
    return {'out': value}
def action_set_value(inputs, state):
    state['manual'] = inputs['args']['value']
    return {'result': state['manual'], 'outputs': {'out': state['manual'] * inputs['__config__']['gain']}}
def action_choose(inputs, state):
    state['selection'] = inputs['args']['value']
    return {'result': state['selection']}
def action_rename(inputs, state):
    state['name'] = inputs['args']['text']
    return {'result': state['name']}
def action_click(inputs, state):
    state['clicks'] = state.get('clicks', 0) + 1
    return {'result': state['clicks']}
'''
        qml = '''import QtQuick
import QtQuick.Controls
Item {
    required property var bridge
    Column { width: parent.width; spacing: 8
        Slider { width: parent.width; from: 0; to: 10; onMoved: bridge.action('set_value', {value:value}) }
        ComboBox { width: parent.width; model:['first','second']; onActivated: bridge.action('choose', {value:currentValue}) }
        SpinBox { width: parent.width; from:0; to:10; onValueModified: bridge.action('set_value', {value:value}) }
        TextField { width: parent.width; placeholderText:'AI Unicode input'; onEditingFinished: bridge.action('rename', {text:text}) }
        Rectangle { width:parent.width; height:48; color:'#315974'; Text {anchors.centerIn:parent; text:'custom mouse area'; color:'white'}
            MouseArea { anchors.fill:parent; onClicked: bridge.action('click', {}) }
        }
        Label { width:parent.width; wrapMode:Text.WrapAnywhere; text:JSON.stringify((bridge.state.state||{}).script_state||{}) }
    }
}'''
        manifest = {'plugin_api': 3, 'id': 'user.ai.verification', 'name': 'AI 控件验收', 'code': code,
                    'ports': [{'id': 'x', 'direction': 'in', 'data_type': 'number'}, {'id': 'out', 'direction': 'out', 'data_type': 'number'}],
                    'properties': {'gain': {'type': 'number', 'default': 2}},
                    'actions': {name: {'args': fields} for name, fields in {'set_value': {'value': {'type': 'number'}}, 'choose': {'value': {'type': 'string'}}, 'rename': {'text': {'type': 'string'}}, 'click': {}}.items()},
                    'views': {'panel': {'qml': 'Panel.qml'}}, 'resources': {'Panel.qml': qml}}
        client.call('plugin.import', {'manifest': manifest})
        client.call('graph.node.add', {'id': 'user', 'plugin': manifest['id'], 'position': [700, 400]})
        client.call('ui.update')
        view = next(i for i in client.call('project.get')['ui']['items'] if i.get('node') == 'user')
        client.call('editor.place', {'page': 'layout', 'id': view['id'], 'parent': 'root', 'position': [16, 240]})
        client.call('ui.item.patch', {'id': view['id'], 'properties': {'width': 600, 'height': 480}})
        client.call('runtime.start', {'debug': False})
        client.call('workspace.open', {'page': 'runtime'})
        time.sleep(.3)
        controls = [c for c in client.call('controls.list', {'page': 'runtime'}) if c['node'] == 'user' and c['visible']]
        unnamed = {kind: next(c for c in controls if c['type'] == kind and not c['name']) for kind in ('QQuickSlider', 'QQuickComboBox', 'QQuickSpinBox', 'QQuickTextField', 'QQuickMouseArea')}
        check(all(c['path'].startswith('auto:runtime:') for c in unnamed.values()), 'unnamed third-party QML controls receive addressable paths')
        check(len({c['path'] for c in controls}) == len(controls), 'generated control paths are unique within the user view')
        client.call('controls.set', {'id': unnamed['QQuickSlider']['id'], 'property': 'value', 'value': 7})
        wait_state('user', lambda s: s['state'].get('script_state', {}).get('manual') == 7)
        client.call('controls.set', {'id': unnamed['QQuickComboBox']['id'], 'property': 'currentIndex', 'value': 1})
        wait_state('user', lambda s: s['state'].get('script_state', {}).get('selection') == 'second')
        client.call('controls.set', {'id': unnamed['QQuickSpinBox']['id'], 'property': 'value', 'value': 9})
        wait_state('user', lambda s: s['state'].get('script_state', {}).get('manual') == 9)
        field = unnamed['QQuickTextField']['id']
        client.call('controls.key', {'id': field, 'key': 'A', 'modifiers': 'ControlModifier'})
        client.call('controls.key', {'id': field, 'text': '中文协作'})
        client.call('controls.commit', {'id': field})
        wait_state('user', lambda s: s['state'].get('script_state', {}).get('name') == '中文协作')
        area = unnamed['QQuickMouseArea']['id']
        client.call('controls.click', {'id': area})
        client.call('controls.pointer', {'id': area, 'type': 'click'})
        wait_state('user', lambda s: s['state'].get('script_state', {}).get('clicks') == 2)
        check(True, 'slider, combo, spinbox, Unicode commit and custom mouse actions reach real Python state')
        request_file = output / 'cli-control.json'
        request_file.write_text(json.dumps({'id': unnamed['QQuickSlider']['id'], 'property': 'value', 'value': 4}), 'utf-8')
        cli = subprocess.run([str(executable.parent / 'CoRig-cli.exe'), 'api', 'controls.set', '--data-dir', str(data),
                              '--json-file', str(request_file), '--wait'], capture_output=True, encoding='utf-8', check=True)
        cli_result = json.loads(cli.stdout)
        check(cli_result['ok'] and cli_result['result']['jobs_complete'], 'native CLI waits for actions started by an unnamed control')
        wait_state('user', lambda s: s['state'].get('script_state', {}).get('manual') == 4)
        client.call('node.action', {'node': 'user', 'action': 'set_value', 'args': {'value': 6}})
        check(client.call('node.state', {'node': 'user'})['outputs']['out'] == 12, 'declared extension action publishes its actual computed result')
        client.call('capture', {'target': 'runtime', 'path': str(output / 'user-controls.png')})
        client.call('runtime.stop')
        client.call('project.save', {'path': str(output / 'UserControls.corig')})
        client.call('graph.node.remove', {'id': 'user'})
        client.call('ui.item.remove', {'id': view['id']})
        client.call('plugin.remove', {'plugin': manifest['id']})
        client.call('project.save', {'path': str(saved)})

        # Native instruments exercise the public view operations and debugger.
        examples = Path(__file__).resolve().parent.parent / 'examples'
        client.call('project.open', {'path': str(examples / 'FunctionLab.corig')})
        client.call('workspace.open', {'page': 'runtime'})
        client.call('runtime.start', {'debug': False})
        time.sleep(.3)
        plot_view = next(iter(registry['plot.curves']['views']))
        attitude_view = next(iter(registry['plot.attitude']['views']))
        client.call('view.actions', {'node': 'plot', 'view': plot_view})
        client.call('view.read', {'node': 'plot', 'view': plot_view})
        view_state = client.call('view.call', {'node': 'plot', 'view': plot_view, 'action': 'range', 'args': {'follow': False, 'x_min': 0, 'x_max': 2}})
        check(not view_state['follow'] and view_state['range']['x_max'] == 2, 'curve range API updates the live view settings')
        client.call('view.call', {'node': 'plot', 'view': plot_view, 'action': 'fft', 'args': {'enabled': True}})
        plot_controls = [c for c in client.call('controls.list', {'page': 'runtime'}) if c['node'] == 'plot' and c['visible']]
        check(any(c.get('text') == '时域' for c in plot_controls), 'FFT API and the visible toolbar show the same mode')
        client.call('view.call', {'node': 'plot', 'view': plot_view, 'action': 'fft', 'args': {'enabled': False}})
        client.call('view.call', {'node': 'attitude', 'view': attitude_view, 'action': 'camera', 'args': {'azimuth': 30, 'elevation': 20, 'zoom': 1.2}})
        client.call('view.call', {'node': 'attitude', 'view': attitude_view, 'action': 'reset_camera'})
        client.call('graph.activity')
        client.call('runtime.pause')
        client.call('runtime.step', {'mode': 'node', 'source': 'time'})
        client.call('runtime.step', {'mode': 'batch', 'source': 'time'})
        client.call('runtime.stop')
        client.call('preferences.set', {'read_only': True})
        client.call('editor.configure', {'page': 'graph', 'zoom': .9})
        reject('graph.node.add', {'id': 'blocked', 'plugin': 'control.number'})
        client.call('preferences.set', {'read_only': False})
        check(True, 'instruments, window controls and read-only design rules remain available through AI commands')
        client.call('workspace.open', {'page': 'layout'})
        client.call('capture', {'target': 'layout', 'path': str(output / 'layout.png')})
        client.call('workspace.open', {'page': 'graph'})
        client.call('graph.fit')
        client.call('capture', {'target': 'graph', 'path': str(output / 'graph.png')})
        client.call('app.quit')
        process.wait(timeout=10)

        missing = sorted(set(schema['commands']) - client.covered)
        check(not missing, 'every advertised command completed successfully: ' + str(len(schema['commands'])) + ('; missing ' + ', '.join(missing) if missing else ''))
        report = {'count': len(checks), 'checks': checks, 'command_count': len(schema['commands']),
                  'covered_commands': sorted(client.covered), 'missing_commands': missing,
                  'executable': str(executable), 'desktop_input_used': False, 'hardware_used': False}
        (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), 'utf-8')
    finally:
        if client is not None and process.poll() is None:
            try:
                client.call('runtime.stop')
                if client.call('app.state')['dirty']:
                    client.call('project.save', {'path': str(output / 'InterruptedCheck.corig')})
                client.call('app.quit')
            except (OSError, RuntimeError):
                pass
        if process.poll() is None:
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=5)


if __name__ == '__main__':
    main()
