"""Paired comparison of two arms measured alternately.

Two changes from the unpaired analysis:

* Pairs by run index. The arms alternate, so run i of each arm saw the same
  machine state; comparing within a pair cancels the drift that dominates the
  unpaired spread. The 2x-spread guard is the right test for unpaired means and
  much too blunt here -- it would reject an effect that every single pair agrees
  on.

* Reports guest cycles per wall second, not just fps. fps depends on how much
  guest work the scene happens to need per frame, which varies slightly between
  restores; cycles/second is throughput of the thing the CPU backend actually
  does. If an arm runs more guest cycles per frame AND more frames per second,
  fps alone understates it.
"""
import json, glob, os, statistics as st

BURST_TOL = 0.03

def load(directory, arms):
    out = {a: {} for a in arms}
    for f in sorted(glob.glob(os.path.join(directory, '*.json'))):
        name = os.path.basename(f)
        arm = name.split('-')[0]
        if arm not in out:
            continue
        d = json.load(open(f))
        sd = d.get('shutdown', {}) or {}
        fr, cy, bu = d.get('frames') or 0, sd.get('cycles') or 0, sd.get('bursts') or 0
        if not (d.get('valid') and fr and cy):
            continue
        index = int(name.split('-')[1].split('.')[0])
        out[arm][index] = {
            'fps': d.get('fps', 0.0),
            'cpf': cy / fr,
            'bpm': bu / (cy / 1e6),
            'cps': d.get('fps', 0.0) * (cy / fr),
        }
    return out


def report(directory, base, test):
    data = load(directory, (base, test))
    allbpm = [r['bpm'] for arm in data.values() for r in arm.values()]
    median = st.median(allbpm)
    pairs = []
    for i in sorted(set(data[base]) & set(data[test])):
        a, b = data[base][i], data[test][i]
        # A run whose dispatcher rate per unit of guest work is off the median
        # executed a different scene; pairing cannot rescue that.
        if max(abs(a['bpm'] - median), abs(b['bpm'] - median)) / median > BURST_TOL:
            print('  pair %d dropped: bursts/Mcycle %.1f vs %.1f, median %.1f'
                  % (i, a['bpm'], b['bpm'], median))
            continue
        pairs.append((i, a, b))

    print('\n  %-4s %10s %10s %8s %12s %12s %8s' %
          ('pair', base, test, 'fps %', base + ' Mc/s', test + ' Mc/s', 'cps %'))
    for i, a, b in pairs:
        print('  %-4d %10.2f %10.2f %+7.1f%% %12.0f %12.0f %+7.1f%%'
              % (i, a['fps'], b['fps'], 100 * (b['fps'] - a['fps']) / a['fps'],
                 a['cps'] / 1e6, b['cps'] / 1e6,
                 100 * (b['cps'] - a['cps']) / a['cps']))

    if not pairs:
        print('  no comparable pairs')
        return
    fps_deltas = [100 * (b['fps'] - a['fps']) / a['fps'] for _, a, b in pairs]
    cps_deltas = [100 * (b['cps'] - a['cps']) / a['cps'] for _, a, b in pairs]
    wins = sum(1 for d in fps_deltas if d > 0)
    print('\n  n=%d pairs' % len(pairs))
    print('  fps   mean %+.1f%%  median %+.1f%%  range %+.1f%% .. %+.1f%%'
          % (st.mean(fps_deltas), st.median(fps_deltas), min(fps_deltas), max(fps_deltas)))
    print('  cyc/s mean %+.1f%%  median %+.1f%%  range %+.1f%% .. %+.1f%%'
          % (st.mean(cps_deltas), st.median(cps_deltas), min(cps_deltas), max(cps_deltas)))
    print('  %d/%d pairs favour %s' % (wins, len(pairs), test))
    # Sign test: probability of this lopsided a split from a coin, both tails.
    from math import comb
    n = len(pairs)
    k = max(wins, n - wins)
    p = 2 * sum(comb(n, j) for j in range(k, n + 1)) / (2 ** n)
    print('  sign test p = %.4f %s' % (min(p, 1.0),
          '(consistent direction)' if p < 0.05 else '(not yet conclusive)'))


if __name__ == '__main__':
    import sys
    report(sys.argv[1], sys.argv[2], sys.argv[3])
