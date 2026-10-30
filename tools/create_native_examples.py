"""Rebuild public examples through the native application's commands.

Use a dedicated, saved QA instance. These projects use official C++ plugins;
neither example needs a Python interpreter when opened by an end user.
"""
from __future__ import annotations

import argparse
from pathlib import Path

from native_ui_check import Client


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parent.parent / 'examples')
    args = parser.parse_args()
    c = Client(args.data_dir)
    if c.call('app.state')['dirty']:
        raise RuntimeError('Refusing to replace unsaved design')
    c.call('runtime.stop')
    c.call('preferences.set', {'read_only': False})
    examples = {
        'SerialPlot': (
            [('serial', 'io.serial', {'port': 'loop://'}, [30, 40]),
             ('console', 'debug.console', {'newline': 'LF'}, [440, 40]),
             ('decode', 'protocol.decode', {'format': 'firewater', 'channels': ['X', 'Y', 'Z'],
               'channel_count': 3, 'sample_rate_hz': 100}, [440, 300]),
             ('plot', 'plot.curves', {}, [850, 300])],
            [('rx', ['serial', 'rx'], ['console', 'rx']),
             ('tx', ['console', 'tx'], ['serial', 'tx']),
             ('bytes', ['console', 'through'], ['decode', 'bytes']),
             ('samples', ['decode', 'samples'], ['plot', 'samples'])]),
        'FunctionLab': (
            [('time', 'source.time', {'period_ms': 10, 'outputs': [{'name': 't', 'clock': 'elapsed', 'unit': 's'}]}, [30, 60]),
             ('signals', 'function.expression', {'inputs': ['t'], 'outputs': {
               'sine': 'sin(2*pi*t)', 'cosine': 'cos(2*pi*t)',
               'roll': '10*cos(t*0.7)', 'pitch': '15*sin(t*0.5)', 'yaw': 't*20'}}, [380, 60]),
             ('plot', 'plot.curves', {'inputs': ['sine', 'cosine'], 'window_seconds': 6}, [850, 40]),
             ('attitude', 'plot.attitude', {'angle_unit': 'deg'}, [850, 380])],
            [('clock', ['time', 't'], ['signals', 't'])] +
            [(key, ['signals', key], ['plot' if key in ('sine', 'cosine') else 'attitude', key])
             for key in ('sine', 'cosine', 'roll', 'pitch', 'yaw')])}
    args.output.mkdir(parents=True, exist_ok=True)
    for name, (nodes, wires) in examples.items():
        c.call('project.new', {'name': name})
        commands = [{'op': 'graph.node.add', 'args': {'id': ident, 'plugin': plugin, 'config': config, 'position': position}}
                    for ident, plugin, config, position in nodes]
        commands += [{'op': 'graph.wire.add', 'args': {'id': ident, 'source': source, 'target': target}}
                     for ident, source, target in wires]
        c.call('batch', {'commands': commands})
        plan = c.call('sync.plan', {'direction': 'graph_to_ui'})
        c.call('sync.apply', {'direction': plan['direction'], 'revision': plan['revision'],
                             'keys': [change['key'] for change in plan['changes']]})
        c.call('ui.item.patch', {'id': 'root', 'properties': {'width': 1280, 'height': 896 if name == 'SerialPlot' else 832, 'grid_size': 16}})
        views = {item['node']: item['id'] for item in c.call('project.get')['ui']['items'] if 'node' in item}
        if name == 'SerialPlot':
            rectangles = {'serial': [16, 16, 352, 320], 'console': [384, 16, 880, 320], 'plot': [16, 352, 1248, 528]}
            for node, (x, y, width, height) in rectangles.items():
                c.call('editor.place', {'page': 'layout', 'id': views[node], 'parent': 'root', 'position': [x, y]})
                c.call('ui.item.patch', {'id': views[node], 'properties': {'width': width, 'height': height}})
        else:
            c.call('ui.container.add', {'id': 'instruments', 'parent': 'root', 'plugin': 'layout.row',
                                      'properties': {'x': 16, 'y': 160, 'width': 1248, 'height': 656, 'title': '曲线与姿态'}})
            for node in ('plot', 'attitude'):
                c.call('editor.place', {'page': 'layout', 'id': views[node], 'parent': 'instruments'})
            c.call('editor.place', {'page': 'layout', 'id': views['time'], 'parent': 'root', 'position': [16, 16]})
            c.call('ui.item.patch', {'id': views['time'], 'properties': {'width': 320, 'height': 112}})
        if not c.call('project.validate')['valid']:
            raise AssertionError(name + ' does not validate')
        if c.call('graph.overlaps'):
            raise AssertionError(name + ' has node spacing warnings')
        destination = (args.output / (name + '.crpj')).resolve()
        c.call('project.save', {'path': str(destination)})
        print(destination, flush=True)
    from native_recording_check import create_design
    create_design(c)
    if c.call('graph.overlaps'):
        raise AssertionError('RecordPlayback has node spacing warnings')
    destination = (args.output / 'RecordPlayback.crpj').resolve()
    c.call('project.save', {'path': str(destination)})
    print(destination, flush=True)


if __name__ == '__main__':
    main()
