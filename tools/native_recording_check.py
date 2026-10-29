"""Verify recording and replay through the real native GUI and local interface.

Use a dedicated instance. All signals are software-generated; captured data,
screenshots and the report stay in the explicit output directory.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import time

from native_ui_check import Client


def create_design(client: Client):
    """Build a reusable signal capture/playback design with an external timer."""
    client.call('runtime.stop')
    client.call('project.new', {'name': 'RecordPlayback'})
    nodes = [
        ('time', 'source.time', {'period_ms': 10, 'outputs': [{'name': 't', 'clock': 'elapsed', 'unit': 's'}]}, [16, 16]),
        ('signals', 'function.expression', {'inputs': ['t'], 'outputs': {'sine': 'sin(2*pi*t)', 'cosine': 'cos(2*pi*t)', 'roll': '10*cos(t)', 'pitch': '15*sin(t*0.5)', 'yaw': 't*20'}}, [320, 16]),
        ('rec', 'data.record', {'data_type': 'samples'}, [640, 16]),
        ('play', 'data.playback', {'data_type': 'samples'}, [960, 16]),
        ('branch', 'data.fork', {'data_type': 'samples'}, [1280, 16]),
        ('extract', 'data.extract', {'channels': {'roll': 'roll', 'pitch': 'pitch', 'yaw': 'yaw'}}, [1580, 300]),
        ('plot', 'plot.curves', {'inputs': [], 'channels': ['sine', 'cosine'], 'window_seconds': 5}, [1580, 16]),
        ('attitude', 'plot.attitude', {'angle_unit': 'deg'}, [1920, 300]),
        ('start', 'control.button', {'label': '录制 5 秒'}, [16, 600]),
        ('start_branch', 'data.fork', {'data_type': 'boolean'}, [320, 600]),
        ('timer', 'source.timer', {'mode': 'countdown', 'interval_ms': 5000}, [640, 600]),
    ]
    wires = [
        ('clock', ['time', 't'], ['signals', 't']),
        ('capture', ['signals', 'samples'], ['rec', 'in']),
        ('live', ['rec', 'out'], ['play', 'in']),
        ('split', ['play', 'out'], ['branch', 'in']),
        ('curve', ['branch', 'a'], ['plot', 'samples']),
        ('angles', ['branch', 'b'], ['extract', 'samples']),
        ('button', ['start', 'trigger'], ['start_branch', 'in']),
        ('record_start', ['start_branch', 'a'], ['rec', 'start']),
        ('timer_start', ['start_branch', 'b'], ['timer', 'start']),
        ('record_pause', ['timer', 'tick'], ['rec', 'pause']),
    ] + [(axis, ['extract', axis], ['attitude', axis]) for axis in ('roll', 'pitch', 'yaw')]
    commands = [{'op': 'graph.node.add', 'args': {'id': ident, 'plugin': plugin, 'config': config, 'position': position}} for ident, plugin, config, position in nodes]
    commands += [{'op': 'graph.wire.add', 'args': {'id': ident, 'source': source, 'target': target}} for ident, source, target in wires]
    client.call('batch', {'commands': commands})
    client.call('ui.item.patch', {'id': 'root', 'properties': {'width': 1280, 'height': 832, 'grid_size': 16}})
    for node, view, box in [('start', 'control', [16, 16, 208, 128]), ('rec', 'recorder', [240, 16, 416, 128]), ('play', 'playback', [672, 16, 592, 128]), ('plot', 'plot', [16, 160, 720, 656]), ('attitude', 'attitude', [752, 160, 512, 656])]:
        x, y, width, height = box
        client.call('ui.view.add', {'id': node + '_view', 'node': node, 'view': view, 'properties': {'x': x, 'y': y, 'width': width, 'height': height, 'title': node}})
    client.call('graph.fit')
    return client.call('project.validate')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--example', type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    client = Client(args.data_dir)
    if client.call('app.state')['dirty']:
        raise RuntimeError('Refusing to replace an unsaved design')
    checks = []

    def check(condition, label):
        if not condition:
            raise AssertionError(label)
        checks.append(label)
        print('PASS', label, flush=True)

    def state(node):
        return client.call('node.state', {'node': node})['state']

    def wait_for(node, predicate, timeout=3):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = state(node)
            if predicate(value):
                return value
            time.sleep(.015)
        raise AssertionError('Timed out waiting for ' + node)

    registry = client.call('registry')
    specs = {entry['id']: entry for entry in registry}
    check(all('logic.' + op in specs for op in ('and', 'or', 'not', 'nand', 'nor', 'xor', 'xnor', 'edge')), 'all logic gates and edge detection are discoverable')
    check(all(port['data_type'] != 'trigger' for entry in registry for port in entry['ports']), 'control signals expose boolean types throughout the registry')
    check(create_design(client)['valid'], 'complete recording, timer, split and instrument design validates')
    window = client.call('app.state')['workspaces']['windows'][0]['id']
    client.call('window.configure', {'window': window, 'state': 'normal', 'width': 1420, 'height': 950})
    for _ in range(100):
        if client.call('app.state')['workspaces']['windows'][0]['frame_count'] > 0:
            break
        time.sleep(.02)
    check(client.call('app.state')['workspaces']['windows'][0]['frame_count'] > 0, 'native window restoration starts actual rendering before capture')
    client.call('project.save', {'path': str(output / 'RecordPlayback.comtoolx')})
    client.call('workspace.open', {'page': 'runtime'})
    client.call('controls.click', {'id': 'execution.run'})
    time.sleep(.15)
    controls = client.call('controls.list')
    check(not any(c['id'] == 'execution.pause' for c in controls), 'engine start and pause share a single control')
    check(client.call('controls.get', {'id': 'execution.run'})['text'].startswith('暂停'), 'engine toggle reflects the real running state')
    wall_start = time.monotonic()
    client.call('controls.click', {'id': 'recording.rec.toggle'})
    wait_for('rec', lambda s: s['recording'] and s['records'] >= 12)
    client.call('controls.click', {'id': 'recording.rec.toggle'})
    paused = state('rec')
    time.sleep(.2)
    after_pause = state('rec')
    check(after_pause['records'] == paused['records'] and after_pause['duration'] == paused['duration'], 'local recording pause freezes capture time while the engine continues')
    live = client.call('node.state', {'node': 'play'})
    check(live['outputs']['out'] == live['inputs']['in'], 'recorder and unloaded playback preserve the original live stream')
    client.call('controls.click', {'id': 'recording.rec.toggle'})
    wait_for('rec', lambda s: s['records'] > paused['records'] + 8)
    capture = output / 'capture.comtoolrec'
    saved = client.call('node.action', {'node': 'rec', 'action': 'save', 'args': {'path': capture.as_uri()}})
    check(capture.is_file() and saved['saved'] and not saved['recording'], 'finish-and-save creates an independent recording file')
    check(saved['duration'] < time.monotonic() - wall_start - .12, 'paused wall time is excluded from the capture timeline')
    client.call('node.action', {'node': 'play', 'action': 'load', 'args': {'path': capture.as_uri()}})
    prepared = state('play')
    time.sleep(.06)
    live = client.call('node.state', {'node': 'play'})
    check(prepared['loaded'] and not prepared['cutoff'] and not prepared['playing'] and live['outputs']['out'] == live['inputs']['in'], 'file preparation does not interrupt live passthrough')
    client.call('controls.set', {'id': 'playback.play.cutoff', 'property': 'checked', 'value': True})
    check(state('play')['cutoff'] and not state('play')['playing'], 'cutoff takes ownership without automatically playing')
    position = saved['duration'] * .45
    client.call('controls.set', {'id': 'playback.play.position', 'property': 'value', 'value': position})
    selected = client.call('node.state', {'node': 'play'})
    check(abs(selected['state']['position'] - position) < .002 and not selected['state']['playing'], 'timeline control seeks immediately while paused')
    sample = selected['outputs']['out']
    check(abs(sample['values']['yaw'] - sample['time'] * 20) < 1e-8, 'replay preserves original sample timestamps and numeric values')
    client.call('controls.set', {'id': 'playback.play.speed', 'property': 'currentIndex', 'value': 4})
    check(state('play')['speed'] == 2, 'speed selector reaches the actual replay scheduler')
    client.call('controls.click', {'id': 'playback.play.toggle'})
    wait_for('play', lambda s: not s['playing'])
    check(state('play')['cutoff'], 'end of file retains source ownership instead of returning to live')
    client.call('controls.set', {'id': 'playback.play.cutoff', 'property': 'checked', 'value': False})
    time.sleep(.06)
    resumed = client.call('node.state', {'node': 'play'})
    check(resumed['outputs']['out'] == resumed['inputs']['in'] and resumed['state']['loaded'], 'disabling cutoff restores live data and retains the imported file')
    client.call('controls.click', {'id': 'execution.run'})
    frozen = client.call('runtime.state')['elapsed']
    time.sleep(.1)
    check(client.call('runtime.state')['elapsed'] == frozen, 'merged engine control performs a real global pause')
    client.call('controls.click', {'id': 'execution.run'})
    client.call('node.action', {'node': 'timer', 'action': 'start', 'args': {'interval_ms': 500}})
    client.call('controls.pointer', {'id': 'control.start', 'type': 'press'})
    check(client.call('controls.get', {'id': 'control.start'})['down'], 'button exposes real pressed feedback before release')
    client.call('capture', {'target': 'runtime', 'path': str(output / 'button-pressed.png')})
    client.call('controls.pointer', {'id': 'control.start', 'type': 'release'})
    wait_for('rec', lambda s: s['recording'])
    check(state('timer')['enabled'], 'one boolean button update starts both recorder and countdown through distribution')
    client.call('node.action', {'node': 'timer', 'action': 'start', 'args': {'interval_ms': 160}})
    time.sleep(.06)
    client.call('controls.click', {'id': 'control.start'})
    time.sleep(.08)
    check(state('rec')['recording'], 'repeated start restarts countdown without clearing the current capture')
    wait_for('rec', lambda s: not s['recording'])
    check(state('rec')['records'] > 0, 'countdown completion pauses recording through its boolean output')
    client.call('runtime.pause')
    client.call('workspace.open', {'page': 'graph'})
    client.call('graph.select', {'id': 'start_branch'})
    time.sleep(.06)
    quantity = client.call('controls.get', {'id': 'property.outputs', 'page': 'graph'})
    check(quantity['from'] == 2 and quantity['to'] == 8, 'distribution quantity is a bounded numeric editor')
    client.call('controls.set', {'id': 'property.outputs', 'page': 'graph', 'property': 'value', 'value': 8})
    project = client.call('project.get')
    check(len(next(n for n in project['nodes'] if n['id'] == 'start_branch')['config']['outputs']) == 8, 'quantity editor configures eight real output ports')
    client.call('controls.set', {'id': 'property.outputs', 'page': 'graph', 'property': 'value', 'value': 2})
    client.call('node.action', {'node': 'play', 'action': 'cutoff', 'args': {'value': True}})
    client.call('node.configure', {'node': 'play', 'changes': {'path': str(capture)}, 'persist': True})
    check(state('play')['cutoff'] and not state('play')['playing'], 'changing the default file preserves explicit cutoff ownership and pauses playback')
    project = client.call('project.get')
    check(not project['resources'], 'default file selection stores a reference without embedding capture history')
    client.call('runtime.reset')
    restored = state('play')
    check(restored['path'] == str(capture).replace('\\', '/') and not restored['cutoff'] and not restored['playing'], 'engine reset reloads the design default in bypass and standby')
    check(state('rec')['records'] == 0 and not state('rec')['recording'], 'engine reset clears unfinished recording without plugin exceptions')
    temporary = output / 'temporary.comtoolrec'
    shutil.copyfile(capture, temporary)
    client.call('node.action', {'node': 'play', 'action': 'load', 'args': {'path': temporary.as_uri()}})
    check(Path(state('play')['path']) == temporary, 'runtime file import is allowed while the engine is idle')
    client.call('runtime.reset')
    check(Path(state('play')['path']) == capture, 'temporary import is discarded by reset in favor of the design default')
    client.call('node.configure', {'node': 'play', 'changes': {'path': ''}, 'persist': True})
    destination = args.example.resolve() if args.example else output / 'RecordPlayback.comtoolx'
    destination.parent.mkdir(parents=True, exist_ok=True)
    client.call('project.save', {'path': str(destination)})
    project = client.call('project.get')
    check(not project['resources'] and next(n for n in project['nodes'] if n['id'] == 'play')['config']['path'] == '', 'public example contains only a reusable design and no recorded data')
    client.call('runtime.start', {'debug': False})
    client.call('node.action', {'node': 'play', 'action': 'load', 'args': {'path': capture.as_uri()}})
    client.call('workspace.open', {'page': 'runtime'})
    client.call('controls.set', {'id': 'playback.play.cutoff', 'property': 'checked', 'value': True})
    client.call('node.action', {'node': 'play', 'action': 'seek', 'args': {'position': saved['duration'] * .5}})
    time.sleep(.08)
    captures = []
    for page in ('runtime', 'graph', 'layout'):
        client.call('workspace.open', {'page': page})
        time.sleep(.08)
        path = output / (page + '.png')
        client.call('capture', {'target': page, 'path': str(path)})
        captures.append(str(path))
    client.call('workspace.open', {'page': 'runtime'})
    check(client.call('runtime.state')['failed'] == 0, 'complete live and replay pipeline has no execution failures')
    report = {'status': 'passed', 'checks': checks, 'plugin_count': len(registry), 'capture': str(capture), 'example': str(destination), 'screenshots': captures}
    (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'status': 'passed', 'checks': len(checks), 'report': str(output / 'report.json')}, ensure_ascii=False))


if __name__ == '__main__':
    main()
