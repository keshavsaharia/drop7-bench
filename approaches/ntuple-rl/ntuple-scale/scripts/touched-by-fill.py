# Seed-free diagnostic over the run-1 checkpoint (weights + coherence
# accumulators): for the row and column families, how many patterns with k
# occupied cells were ever updated, by rise phase, and what value the
# touched vs untouched entries carry.  Reads no gameplay data.
import numpy as np, sys, json, time
import argparse
parser = argparse.ArgumentParser(description="Seed-free: touched table entries of a run-1-layout checkpoint by pattern fill (cited by TH-20260906-ntuple-fill-conditioned-leaf-28cb0ae2).")
parser.add_argument("--checkpoint", default="runs/RUN-20260905T193006Z-4fbeb4e5/ntuple-scale/main/checkpoint.bin")
parser.add_argument("--out", default=None)
args = parser.parse_args()
path = args.checkpoint
f = open(path, 'rb')
magic = f.read(8); assert magic == b'D7NTUP01', magic
n = int.from_bytes(f.read(4), 'little'); spec = f.read(n).decode()
count = int.from_bytes(f.read(8), 'little'); flag = f.read(1)[0]
hdr = 8 + 4 + n + 8 + 1
print('layout', spec, 'entries', count, 'has_tc', flag, 'header', hdr)
W = np.memmap(path, dtype=np.float32, mode='r', offset=hdr, shape=(count,))
A = np.memmap(path, dtype=np.float32, mode='r', offset=hdr + 8 * count, shape=(count,))
LINE = 10_000_000; PH = 5
# per-pattern occupied-cell count and gray count for a seven-cell base-10 pattern
idx = np.arange(LINE, dtype=np.int64)
digits = np.stack([(idx // 10**k) % 10 for k in range(7)], axis=1).astype(np.int8)
occ = (digits != 0).sum(axis=1).astype(np.int8)          # 0..7
gray = ((digits == 8) | (digits == 9)).sum(axis=1).astype(np.int8)
# contiguous-from-bottom check is only meaningful for columns (gravity): a
# legal column pattern has digits nonzero for the low rows then zero above.
# nibble n = row (6-n) so digit 0 is the bottom row.
legal_col = np.ones(LINE, dtype=bool)
seen_zero = np.zeros(LINE, dtype=bool)
for k in range(7):
    d = digits[:, k] != 0
    legal_col &= ~(seen_zero & d)
    seen_zero |= ~d
init = np.float32(20.0 / 74)
out = {'layout': spec, 'families': {}}
t0 = time.time()
for fam, off, tables in (('rows', 0, 7), ('cols', 7 * PH * LINE, 7)):
    fam_out = {}
    for t in range(tables):
        for ph in range(PH):
            s = off + t * PH * LINE + ph * LINE
            a = np.asarray(A[s:s + LINE]); w = np.asarray(W[s:s + LINE])
            touched = a > 0
            row = {}
            for k in range(8):
                m = occ == k
                if fam == 'cols': m &= legal_col
                tk = touched & m
                nt = int(tk.sum()); nm = int(m.sum())
                row[k] = {'patterns': nm, 'touched': nt,
                          'meanW_touched': float(w[tk].mean()) if nt else None,
                          'meanA_touched': float(a[tk].mean()) if nt else None}
            fam_out[f'{fam}{t}/phase{ph+1}'] = row
        print(fam, t, f'{time.time()-t0:.0f}s', file=sys.stderr)
    out['families'][fam] = fam_out
# aggregate: by family x occupied count (summing tables and phases), and by phase
def agg(fam, keyfn):
    acc = {}
    for key, row in out['families'][fam].items():
        g = keyfn(key)
        for k, r in row.items():
            a = acc.setdefault((g, k), [0, 0, 0.0, 0.0])
            a[0] += r['patterns']; a[1] += r['touched']
            if r['touched']:
                a[2] += r['meanW_touched'] * r['touched']; a[3] += r['meanA_touched'] * r['touched']
    return acc
print('\n== touched patterns by occupied cells (all tables, all phases); meanW in rise units (init %.4f), meanA = accumulated |error|' % init)
for fam in ('rows', 'cols'):
    acc = agg(fam, lambda k: 'all')
    print(f'-- {fam}')
    print('occ  patterns     touched   frac      meanW_touched  meanA_touched')
    for k in range(8):
        p, t, ws, as_ = acc[('all', k)]
        print(f'{k:3d} {p:11d} {t:10d} {t/p if p else 0:8.5f}   {ws/t if t else float("nan"):12.4f} {as_/t if t else float("nan"):12.2f}')
print('\n== touched by phase (moves until rise), rows+cols')
for fam in ('rows', 'cols'):
    acc = agg(fam, lambda k: k.split('/')[1])
    for ph in range(1, 6):
        tot = sum(acc[(f'phase{ph}', k)][1] for k in range(8)); pat = sum(acc[(f'phase{ph}', k)][0] for k in range(8))
        print(f'{fam} phase{ph}: touched {tot:10d} of {pat}  ({tot/pat:.5f})')
print('\n== rows by row index (0 = top), all phases')
acc = agg('rows', lambda k: k.split('/')[0])
for t in range(7):
    tot = sum(acc[(f'rows{t}', k)][1] for k in range(8))
    hi = sum(acc[(f'rows{t}', k)][1] for k in range(5, 8)); hip = sum(acc[(f'rows{t}', k)][0] for k in range(5, 8))
    print(f'row{t}: touched {tot:10d}; patterns with >=5 discs touched {hi:9d} of {hip} ({hi/hip:.5f})')
print('\n== cols by height (occupied cells = height), all phases, legal patterns only')
acc = agg('cols', lambda k: 'all')
if args.out:
    json.dump({str(k): v for k, v in out['families'].items()}, open(args.out, 'w'))
