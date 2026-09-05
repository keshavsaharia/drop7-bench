#!/usr/bin/env python3
"""Retain paired CHECK measurements; run after correctness gates, without builds.

macOS /usr/bin/time -l reports whole-process retired instructions, cycles, CPU,
and peak RSS. Needs read permission for kern.clockrate. No root/game generation.
Every invocation has a timeout, every stdout/stderr is retained, and semantic
mismatches stop the batch. Timing is descriptive on an interactive host.
"""
import argparse
import hashlib
import json
import os
import pathlib
import platform
import re
import signal
import statistics
import subprocess
import time


def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def counters(stderr):
    result = {}
    for field, label in [('peakRssBytes', 'maximum resident set size'),
                         ('instructions', 'instructions retired'),
                         ('cycles', 'cycles elapsed'), ('pageFaults', 'page faults'),
                         ('involuntarySwitches', 'involuntary context switches')]:
        m = re.search(r'^\s*(\d+)\s+' + label + r'\s*$', stderr, re.M)
        result[field] = int(m[1]) if m else None
    m = re.search(r'([\d.]+) real\s+([\d.]+) user\s+([\d.]+) sys', stderr)
    result['processCpuSeconds'] = float(m[2]) + float(m[3]) if m else None
    return result


def semantic(rows):
    if rows[0].get('mode') in ('micro', 'unpack'):
        return [(r['mode'], r['calls'], r['checksum']) for r in rows]
    if rows[0].get('mode') == 'search':
        return [(r['root'], r['depth'], r['action'], r['values']) for r in rows]
    return [(r['root'], r['depth'], r['selectedAction'],
             [(c['column'], c['valueBits']) for c in r['columns']])
            for r in rows if r.get('recordType') == 'decision']


def run(args):
    assert platform.system() == 'Darwin', 'This counter adapter requires macOS /usr/bin/time -l.'
    out = pathlib.Path(args.output)
    out.mkdir(parents=True, exist_ok=False)
    spec = vars(args).copy()
    spec['hashes'] = {p: sha(p) for p in [args.baseline, args.candidate, args.roots, __file__]}
    if args.analyzer:
        spec['hashes'][args.analyzer] = sha(args.analyzer)
    spec['hostTiming'] = 'Interactive shared macOS host; one benchmark process, no concurrent task builds/tests. No affinity or exclusive-machine guarantee.'
    (out / 'config.json').write_text(json.dumps(spec, indent=2) + '\n')
    jobs = []
    if args.phase == 'compare':
        for mode, depth, gate in [('unpack', 4, 1), ('micro', 4, 1), ('search', 3, 1), ('search', 4, 1), ('search', 4, 2)]:
            for repeat in range(args.repeats):
                arms = [('baseline', args.baseline), ('candidate', args.candidate)]
                if repeat % 2: arms.reverse()
                for arm, binary in arms:
                    jobs.append((f'{mode}-d{depth}-g{gate}', arm, repeat,
                                 [binary, mode, args.roots, str(depth), str(gate), '16384', '200000']))
    elif args.phase == 'sweep':
        arms = [(gate, cap) for gate in [1, 2, 3] for cap in [1024, 16384, 262144]] + [(0, 0)]
        for repeat in range(args.repeats):
            for gate, cap in (arms if repeat % 2 == 0 else list(reversed(arms))):
                jobs.append(('cache-sweep', f'g{gate}-c{cap}', repeat,
                             [args.candidate, 'search', args.roots, '4', str(gate), str(cap), '1']))
    else:
        assert args.analyzer
        for workers in [1, 2, 4]:
            for gate in [1, 2]:
                for repeat in range(args.repeats):
                    scopes = ['private', 'shared'] if repeat % 2 == 0 else ['shared', 'private']
                    for scope in scopes:
                        # Equal aggregate entries, private partitions the total.
                        capacity = 65536 // workers if scope == 'private' else 65536
                        jobs.append((f'parallel-w{workers}-g{gate}', scope, repeat,
                                     [args.analyzer, '--roots', args.roots, '--output', '-', '--depths', '4', '--strata', '7',
                                      '--threads', str(workers), '--cache', str(capacity), '--tt-scope', scope,
                                      '--tt-from-depth', str(gate), '--split-plies', '0', '--max-host-bytes', '4294967296']))
    seen, retained = {}, []
    prior_seconds = sum(json.loads(line)['elapsedSeconds']
                        for path in out.parent.glob('*/measurements.jsonl')
                        for line in path.read_text().splitlines())
    started = time.monotonic()
    for index, (group, arm, repeat, command) in enumerate(jobs):
        remaining = 1800 - prior_seconds - (time.monotonic() - started)
        if remaining <= 0:
            raise RuntimeError('1800 second batch budget reached; partial records retained')
        tag = f'{index:03}-{group}-{arm}-r{repeat}'
        start = time.monotonic()
        proc = subprocess.Popen(['/usr/bin/time', '-l', *command], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True, start_new_session=True)
        try:
            stdout, stderr = proc.communicate(timeout=min(120, remaining))
        except subprocess.TimeoutExpired as e:
            # /usr/bin/time has a child: terminate the entire task-owned group,
            # then reap the wrapper and retain the partial measurement output.
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            stdout, stderr = proc.communicate()
            (out / f'{tag}.stdout').write_text(stdout)
            (out / f'{tag}.stderr').write_text(stderr)
            (out / f'{tag}.timeout.json').write_text(json.dumps({'command': command, 'status': 'partial', 'reason': 'process or shared batch time budget reached'}))
            raise RuntimeError('timeout; partial batch retained') from e
        (out / f'{tag}.stdout').write_text(stdout)
        (out / f'{tag}.stderr').write_text(stderr)
        record = {'group': group, 'arm': arm, 'repeat': repeat, 'command': command, 'exitCode': proc.returncode,
                  'elapsedSeconds': time.monotonic() - start, **counters(stderr)}
        if proc.returncode:
            (out / f'{tag}.failure.json').write_text(json.dumps(record, indent=2))
            raise RuntimeError(f'{tag} failed; outputs retained')
        rows = [json.loads(line) for line in stdout.splitlines() if line.startswith('{')]
        key = 'd4' if args.phase == 'sweep' else group
        current = semantic(rows)
        assert current, f'{tag}: no semantic rows'
        assert current == seen.setdefault(key, current), f'{tag}: semantic mismatch'
        record['rows'] = rows
        record['searchSeconds'] = sum(r.get('seconds', r.get('metrics', {}).get('wallSeconds', 0)) for r in rows)
        record['work'] = sum(r.get('work', r.get('metrics', {}).get('work', 0)) for r in rows)
        retained.append(record)
        with (out / 'measurements.jsonl').open('a') as f:
            f.write(json.dumps(record) + '\n')
        print(f'{tag}: {record["searchSeconds"]:.4f}s, {record["instructions"]} instructions', flush=True)
    summary = []
    for group, arm in dict.fromkeys((r['group'], r['arm']) for r in retained):
        rows = [r for r in retained if (r['group'], r['arm']) == (group, arm)]
        entry = {'group': group, 'arm': arm, 'repeats': len(rows)}
        for field in ['searchSeconds', 'processCpuSeconds', 'instructions', 'cycles', 'peakRssBytes', 'work']:
            values = [r[field] for r in rows if r[field] is not None]
            entry[field] = {'median': statistics.median(values), 'min': min(values), 'max': max(values)} if values else None
        summary.append(entry)
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline', required=True)
    p.add_argument('--candidate', required=True)
    p.add_argument('--analyzer')
    p.add_argument('--roots', required=True)
    p.add_argument('--output', required=True)
    p.add_argument('--phase', choices=['compare', 'sweep', 'parallel'], required=True)
    p.add_argument('--repeats', type=int, default=5)
    a = p.parse_args()
    assert 3 <= a.repeats <= 5
    run(a)
