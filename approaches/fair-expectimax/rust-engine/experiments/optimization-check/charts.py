#!/usr/bin/env python3
"""Copy recorded CHECK measurements into the web figure format; no site math."""
import argparse
import json
import pathlib

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('result', type=pathlib.Path)
a = p.parse_args()
r = json.loads(a.result.read_text())
rid = r['resultId']
out = pathlib.Path('web/content/figures')
notes = ('Six constructed public roots per repeat, five alternating-order repeats on an interactive Apple M3 Pro. '
         'Depth 4, seven chance strata, completed searches with exact value checks. '
         'Whiskers show the minimum and maximum repeat times, not confidence intervals. '
         'CHECK mechanics evidence; these roots and elapsed times do not measure playing strength. '
         'The table view lists the plotted values and their sources.')
series = []
for gate in [1, 2, 3]:
    points = []
    for i, row in enumerate(r['metrics']['cacheSweep']):
        if not row['arm'].startswith(f'g{gate}-'):
            continue
        points.append({'x': row['tableBytes'][0], 'y': row['searchSeconds']['median'],
                       'lo': row['searchSeconds']['min'], 'hi': row['searchSeconds']['max'],
                       'sourceRecord': rid, 'sourceField': f'metrics.cacheSweep[{i}].searchSeconds.median'})
    series.append({'name': f'Cache from depth {gate}', 'points': points})
spec = {'title': 'Cache capacity and search time', 'kind': 'line',
        'x': {'label': 'Allocated private table bytes', 'unit': 'bytes', 'scale': 'log'},
        'y': {'label': 'Median seconds per six-root batch', 'unit': 's'}, 'notes': notes,
        'series': series}
(out / 'rust-cache-capacity.json').write_text(json.dumps(spec, indent=2) + '\n')
series = []
for scope in ['private', 'shared']:
    points = []
    for i, row in enumerate(r['metrics']['parallelSweep']):
        if row['arm'] != scope:
            continue
        _, w, g = row['group'].split('-')
        points.append({'x': f"{w[1:]} worker{'s' if w != 'w1' else ''}, gate {g[1:]}", 'y': row['searchSeconds']['median'],
                       'lo': row['searchSeconds']['min'], 'hi': row['searchSeconds']['max'],
                       'sourceRecord': rid, 'sourceField': f'metrics.parallelSweep[{i}].searchSeconds.median'})
    series.append({'name': scope.capitalize(), 'points': points})
spec = {'title': 'Private and shared search caches', 'kind': 'bar',
        'x': {'label': 'Workers and minimum cached depth'},
        'y': {'label': 'Median seconds per six-root batch', 'unit': 's'},
        'notes': notes + ' Both scopes have 65,536 total entries. Shared storage includes an additional bounded stripe-lock allowance. The frontier split is fixed at zero internal plies.',
        'series': series}
(out / 'rust-shared-cache.json').write_text(json.dumps(spec, indent=2) + '\n')
