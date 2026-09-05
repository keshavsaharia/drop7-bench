#!/usr/bin/env python3
"""Derive compact descriptive CHECK evidence from all retained repetitions."""
import argparse
import hashlib
import json
import pathlib
import statistics as stats


def spread(values):
    values = [v for v in values if v is not None]
    return {'median': stats.median(values), 'min': min(values), 'max': max(values)} if values else None


def summarize(run):
    result = {'format': 'drop7-rust-optimization-check-v1', 'rootsPerRepeat': 6,
              'repeats': 5, 'strata': 7, 'machine': 'Apple M3 Pro',
              'timingScope': 'Interactive shared host, descriptive CHECK; no gameplay or strength claim.',
              'wallMetricScope': {'compareAndSweep': 'Sum of six search timers, excluding table allocation and process startup; micro/unpack use their inner-loop timer.',
                                  'parallel': 'Sum of six decision wall timers, including planning, allocation, execution and reduction, excluding process startup.'},
              'counterScope': 'Whole measured process including harness, setup and output; macOS printed CPU seconds have 0.01 second resolution.',
              'phases': {}, 'comparisons': [], 'cacheSweep': [], 'parallelSweep': [], 'parallelComparisons': []}
    total = 0
    for phase in ['leaf-compare', 'compare', 'sweep', 'parallel']:
        rows = [json.loads(s) for s in (run / phase / 'measurements.jsonl').read_text().splitlines()]
        expected = 60 if phase == 'parallel' else 50
        assert len(rows) == expected, f'{phase}: partial batch {len(rows)}/{expected}'
        assert all(r['exitCode'] == 0 for r in rows)
        total += sum(r['elapsedSeconds'] for r in rows)
        groups = {}
        for group, arm in dict.fromkeys((r['group'], r['arm']) for r in rows):
            samples = [r for r in rows if (r['group'], r['arm']) == (group, arm)]
            assert sorted(r['repeat'] for r in samples) == list(range(5))
            sample_rows = [r for sample in samples for r in sample['rows'] if r.get('mode') == 'search' or r.get('recordType') == 'decision']
            entry = {'group': group, 'arm': arm, 'repeats': 5}
            for field in ['searchSeconds', 'processCpuSeconds', 'instructions', 'cycles', 'peakRssBytes', 'work']:
                entry[field] = spread([r[field] for r in samples])
            entry['processElapsedSeconds'] = spread([r['elapsedSeconds'] for r in samples])
            entry['initializationSeconds'] = spread([
                sum(r.get('initializationSeconds', r.get('metrics', {}).get('initializationSeconds', 0)) for r in s['rows'])
                for s in samples if any('initializationSeconds' in r or 'initializationSeconds' in r.get('metrics', {}) for r in s['rows'])
            ])
            entry['tableBytes'] = sorted(set(r.get('tableBytes', r.get('metrics', {}).get('projectedTableBytes', 0)) for r in sample_rows))
            entry['leafCalls'] = spread([sum(r['calls'] if r.get('mode') == 'micro' else r.get('leafCalls', r.get('metrics', {}).get('leafCalls', 0)) for r in s['rows']) for s in samples])
            entry['cacheHits'] = spread([sum(r.get('cacheHits', r.get('metrics', {}).get('cacheHits', 0)) for r in s['rows']) for s in samples])
            groups[(group, arm)] = entry
        result['phases'][phase] = list(groups.values())
        if phase in ['compare', 'leaf-compare']:
            for group in dict.fromkeys(r['group'] for r in rows):
                before, after = groups[(group, 'baseline')], groups[(group, 'candidate')]
                b = sorted([r for r in rows if r['group'] == group and r['arm'] == 'baseline'], key=lambda r: r['repeat'])
                a = sorted([r for r in rows if r['group'] == group and r['arm'] == 'candidate'], key=lambda r: r['repeat'])
                entry = {'phase': phase, 'group': group,
                         'wallSpeedRatio': before['searchSeconds']['median'] / after['searchSeconds']['median'],
                         'pairedWallSpeedRatios': spread([x['searchSeconds'] / y['searchSeconds'] for x, y in zip(b, a)]),
                         'instructionReductionPercent': 100 * (1 - after['instructions']['median'] / before['instructions']['median']),
                         'baselineSeconds': before['searchSeconds']['median'], 'candidateSeconds': after['searchSeconds']['median'],
                         'baselineInstructions': before['instructions']['median'], 'candidateInstructions': after['instructions']['median']}
                result['comparisons'].append(entry)
        elif phase == 'sweep':
            result['cacheSweep'] = list(groups.values())
        else:
            result['parallelSweep'] = list(groups.values())
            for group in dict.fromkeys(r['group'] for r in rows):
                private, shared = groups[(group, 'private')], groups[(group, 'shared')]
                p = sorted([r for r in rows if r['group'] == group and r['arm'] == 'private'], key=lambda r: r['repeat'])
                s = sorted([r for r in rows if r['group'] == group and r['arm'] == 'shared'], key=lambda r: r['repeat'])
                result['parallelComparisons'].append({
                    'group': group,
                    'wallSpeedRatio': private['searchSeconds']['median'] / shared['searchSeconds']['median'],
                    'pairedWallSpeedRatios': spread([a['searchSeconds'] / b['searchSeconds'] for a, b in zip(p, s)]),
                    'workReductionPercent': 100 * (1 - shared['work']['median'] / private['work']['median']),
                    'instructionReductionPercent': 100 * (1 - shared['instructions']['median'] / private['instructions']['median']),
                })
    result['timedProcessCount'] = 210
    result['timedProcessSeconds'] = total
    assert total <= 1800, 'protocol timing budget exceeded'
    result['artifactSha256'] = {str(path.relative_to(run)): hashlib.sha256(path.read_bytes()).hexdigest()
                               for path in sorted(run.glob('*/measurements.jsonl'))}
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('run', type=pathlib.Path)
    args = p.parse_args()
    (args.run / 'summary.json').write_text(json.dumps(summarize(args.run), indent=2) + '\n')
