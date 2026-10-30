"""Measure actual native computation, view delivery and rendered frames.

The same saved graph runs with wire animation off/on in alternating order.
This is a desktop software benchmark, not a hardware or serial timing test.
"""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import statistics
import time

from native_ui_check import Client


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=4)
    args = parser.parse_args()
    c = Client(args.data_dir)
    if c.call('app.state')['dirty']:
        raise RuntimeError('Refusing to replace unsaved design')
    args.output.mkdir(parents=True, exist_ok=True)
    c.call('runtime.stop')
    c.call('preferences.set', {'read_only': False})
    c.call('project.new', {'name': 'NativePerformance'})
    operations = []
    # Twenty independent always-ready chains saturate the real graph worker.
    # A separate 100 Hz source feeds a visible curve throughout every run.
    for i in range(20):
        nodes = [(f's{i}', 'source.constant', {'values': {'value': 1.25}}),
                 (f'f{i}', 'function.sin', {}), (f'v{i}', 'display.value', {})]
        for j, (ident, plugin, config) in enumerate(nodes):
            operations.append({'op': 'graph.node.add', 'args': {'id': ident, 'plugin': plugin,
                'config': config, 'position': [j * 280, i * 140]}})
        for key, source, target in [(f'a{i}', [f's{i}', 'value'], [f'f{i}', 'x']),
                                    (f'b{i}', [f'f{i}', 'out'], [f'v{i}', 'value'])]:
            operations.append({'op': 'graph.wire.add', 'args': {'id': key, 'source': source, 'target': target}})
    operations += [
        {'op': 'graph.node.add', 'args': {'id': 'clock', 'plugin': 'source.time',
             'config': {'period_ms': 10, 'outputs': [{'name': 't', 'clock': 'elapsed', 'unit': 's'}]}, 'position': [0, 2940]}},
        {'op': 'graph.node.add', 'args': {'id': 'plot', 'plugin': 'plot.curves',
             'config': {'inputs': ['time'], 'max_points': 5000}, 'position': [280, 2940]}},
        {'op': 'graph.wire.add', 'args': {'id': 'observed', 'source': ['clock', 't'], 'target': ['plot', 'time']}},
        {'op': 'ui.view.add', 'args': {'id': 'curve', 'parent': 'root', 'node': 'plot', 'view': 'plot', 'properties': {'min_height': 500}}}]
    c.call('batch', {'commands': operations})
    c.call('project.save', {'path': str((args.output / 'Performance.crpj').resolve())})
    c.call('workspace.open', {'page': 'runtime'})
    runtime_window = c.call('workspace.detach', {'page': 'runtime', 'x': 600, 'y': 120})['window']
    c.call('workspace.open', {'page': 'graph'})
    c.call('graph.fit')
    c.call('log.configure', {'detail': False})

    # Read CPU time from the actual process. No extra dependency or system-wide
    # counter is required, and the process handle is closed even on failure.
    record = json.loads((args.data_dir / '.sessions/api.json').read_text('utf-8'))
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.OpenProcess(0x1000, False, record['pid'])
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())

    def cpu_seconds():
        created, exited, system, user = (wintypes.FILETIME() for _ in range(4))
        if not kernel.GetProcessTimes(handle, ctypes.byref(created), ctypes.byref(exited), ctypes.byref(system), ctypes.byref(user)):
            raise ctypes.WinError(ctypes.get_last_error())
        return ((system.dwHighDateTime + user.dwHighDateTime) * 2**32 + system.dwLowDateTime + user.dwLowDateTime) / 1e7

    samples = []
    try:
        for enabled in (False, True, True, False):
            c.call('runtime.reset')
            c.call('controls.set', {'id': 'graph.activity', 'property': 'checked', 'value': enabled})
            c.call('runtime.start', {'debug': False})
            time.sleep(.5)
            before, state0 = c.call('runtime.state'), c.call('app.state')
            cpu0, started = cpu_seconds(), time.perf_counter()
            time.sleep(args.seconds)
            cpu1, ended = cpu_seconds(), time.perf_counter()
            after, state1 = c.call('runtime.state'), c.call('app.state')
            elapsed = ended - started
            windows0 = {w['id']: w for w in state0['workspaces']['windows']}
            windows1 = {w['id']: w for w in state1['workspaces']['windows']}
            frames = {ident: (w['frame_count'] - windows0[ident]['frame_count']) / elapsed for ident, w in windows1.items()}
            intervals = windows1[runtime_window]['frame_intervals_ms'][-max(1, int(frames[runtime_window] * elapsed)):]
            intervals.sort()
            sample = {'animation': enabled, 'duration_s': elapsed,
                'node_executions_s': (after['processed'] - before['processed']) / elapsed,
                'source_batches_s': (after['batch'] - before['batch']) / elapsed,
                'cpu_percent_one_core': (cpu1 - cpu0) / elapsed * 100,
                'plot_observations_s': (state1['view_observations'].get('plot/plot', 0) - state0['view_observations'].get('plot/plot', 0)) / elapsed,
                'curve_acquisition_hz': (after['nodes']['plot']['executions'] - before['nodes']['plot']['executions']) / elapsed,
                'rendered_frames_s': frames,
                'curve_frame_interval_p95_ms': intervals[int((len(intervals) - 1) * .95)],
                'failed_delta': after['failed'] - before['failed']}
            if sample['failed_delta'] or sample['curve_acquisition_hz'] < 80 or sample['plot_observations_s'] < 10:
                raise AssertionError('Real computation or observation stalled: ' + str(sample))
            samples.append(sample)
            print(json.dumps(sample, ensure_ascii=False), flush=True)
            c.call('runtime.stop')
        summary = {str(enabled).lower(): {key: statistics.mean(s[key] for s in samples if s['animation'] == enabled)
                    for key in ('node_executions_s', 'cpu_percent_one_core', 'plot_observations_s', 'curve_acquisition_hz', 'curve_frame_interval_p95_ms')}
                   for enabled in (False, True)}
        summary['throughput_change_percent'] = (summary['true']['node_executions_s'] / summary['false']['node_executions_s'] - 1) * 100
        (args.output / 'report.json').write_text(json.dumps({'method': __doc__, 'samples': samples, 'summary': summary}, ensure_ascii=False, indent=2), 'utf-8')
    finally:
        kernel.CloseHandle(handle)
        c.call('runtime.stop')
        main_window = next(w['id'] for w in c.call('app.state')['workspaces']['windows'] if not w['child'])
        c.call('workspace.move', {'page': 'runtime', 'window': main_window})
        c.call('project.save')


if __name__ == '__main__':
    main()
