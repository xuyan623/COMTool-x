"""Check actual rendered wire motion through public observations and stepping."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import time

from native_ui_check import Client


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    c = Client(args.data_dir)
    if c.call('app.state')['dirty']:
        raise RuntimeError('Refusing to replace unsaved design')
    c.call('runtime.stop')
    c.call('project.new', {'name': 'WireAnimation'})
    c.call('batch', {'commands': [
        {'op': 'graph.node.add', 'args': {'id': 'time', 'plugin': 'source.time', 'config': {
            'period_ms': 200, 'outputs': [{'name': 't', 'clock': 'elapsed', 'unit': 's'}]}, 'position': [0, 0]}},
        {'op': 'graph.node.add', 'args': {'id': 'sin', 'plugin': 'function.sin', 'position': [400, 0]}},
        {'op': 'graph.node.add', 'args': {'id': 'value', 'plugin': 'display.value', 'position': [800, 0]}},
        {'op': 'graph.wire.add', 'args': {'id': 'first', 'source': ['time', 't'], 'target': ['sin', 'x']}},
        {'op': 'graph.wire.add', 'args': {'id': 'second', 'source': ['sin', 'out'], 'target': ['value', 'value']}}]})
    c.call('workspace.open', {'page': 'graph'})
    c.call('graph.fit')
    c.call('controls.set', {'id': 'graph.activity', 'property': 'checked', 'value': True})
    checks = []

    def check(condition, message):
        if not condition:
            raise AssertionError(message)
        checks.append(message)
        print('PASS', message, flush=True)

    c.call('runtime.start', {'debug': False})
    time.sleep(1)
    slow = c.call('graph.activity')['wires']['first']
    c.call('runtime.pause')
    c.call('node.configure', {'node': 'time', 'changes': {'period_ms': 10}})
    c.call('runtime.start', {'debug': False})
    time.sleep(1)
    fast = c.call('graph.activity')['wires']['first']
    check(fast['speed_px_s'] > slow['speed_px_s'] and fast['spacing_px'] < slow['spacing_px'],
          'actual transfer frequency raises bar speed and density')
    check(fast['spacing_px'] - fast['bar_length_px'] >= 12, 'high-frequency bars retain a visible gap')
    c.call('runtime.pause')
    time.sleep(.1)
    frozen = c.call('graph.activity')['wires']['first']['bar_positions_px']
    check(bool(frozen), 'pausing retains visible bars')
    time.sleep(.15)
    check(c.call('graph.activity')['wires']['first']['bar_positions_px'] == frozen, 'paused normal bars do not move')
    c.call('runtime.start', {'debug': False})
    time.sleep(.15)
    check(c.call('graph.activity')['wires']['first']['bar_positions_px'] != frozen, 'continuing resumes actual bar motion')
    c.call('runtime.reset')
    c.call('runtime.debug', {'enabled': True})
    c.call('runtime.step', {'mode': 'node', 'source': 'time'})
    time.sleep(.06)
    stepped = c.call('graph.activity')['wires']
    check(stepped['first']['step_count'] == 1 and bool(stepped['first']['bar_positions_px'])
          and not stepped['second']['bar_positions_px'], 'a node step animates only the wire that transferred')
    c.call('capture', {'target': 'graph', 'path': str((args.output / 'single-step.png').resolve())})
    c.call('runtime.stop')
    time.sleep(.08)
    check(all(not w['bar_positions_px'] for w in c.call('graph.activity')['wires'].values()), 'ending the session clears remaining motion')
    c.call('project.save', {'path': str((args.output / 'Animation.comtoolx').resolve())})
    (args.output / 'report.json').write_text(json.dumps({'checks': checks, 'count': len(checks), 'slow': slow, 'fast': fast}, ensure_ascii=False, indent=2), 'utf-8')


if __name__ == '__main__':
    main()
