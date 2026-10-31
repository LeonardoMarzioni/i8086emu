# Conta, nei file della suite indicati, i valori di FLAGS (stato iniziale e finale) per
# combinazione dei bit riservati: 1 e 12..15 (maschera F002h), 3 e 5 (maschera 0028h).
import collections, json, sys

total = collections.Counter()
for path in sys.argv[1:]:
    if path.endswith('metadata.json'):
        continue  # non è un file di test: così si può passare data/ssts/*.json
    seen = collections.Counter()
    for t in json.load(open(path)):
        for state in ('initial', 'final'):
            f = t[state]['regs'].get('flags')
            if f is not None:
                seen[(f & 0xF002, f & 0x0028)] += 1
    print(path, {f'{a:04X}/{b:04X}': n for (a, b), n in sorted(seen.items())})
    total.update(seen)
print('totale', {f'{a:04X}/{b:04X}': n for (a, b), n in sorted(total.items())})
