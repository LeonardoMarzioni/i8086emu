# Verifica usa-e-getta di indirizzo effettivo, segmenti e prefissi contro SingleStepTests.
import collections, gzip, json, os, sys

data_dir = sys.argv[1]
REG16 = ['ax', 'cx', 'dx', 'bx', 'sp', 'bp', 'si', 'di']
SEGS = ['es', 'cs', 'ss', 'ds']
PREFIX = {0x26: 'es', 0x2E: 'cs', 0x36: 'ss', 0x3E: 'ds'}
C, P, A, Z, S, O = 0x001, 0x004, 0x010, 0x040, 0x080, 0x800


def phys(seg, off):
    return ((seg << 4) + off) & 0xFFFFF


class State:
    def __init__(self, t):
        self.r = dict(t['initial']['regs'])
        self.mem = {a: v for a, v in t['initial']['ram']}
        self.writes = {}

    def rd8(self, seg, off):
        return self.mem[phys(self.r[seg], off & 0xFFFF)]

    def rd16(self, seg, off):
        return self.rd8(seg, off) | (self.rd8(seg, off + 1) << 8)

    def wr8(self, seg, off, v):
        a = phys(self.r[seg], off & 0xFFFF)
        self.mem[a] = v
        self.writes[a] = v

    def wr16(self, seg, off, v):
        self.wr8(seg, off, v & 0xFF)
        self.wr8(seg, off + 1, v >> 8)

    def get(self, idx, w):
        if w == 16:
            return self.r[REG16[idx]]
        v = self.r[REG16[idx & 3]]
        return (v >> 8) if idx & 4 else v & 0xFF

    def put(self, idx, w, val):
        if w == 16:
            self.r[REG16[idx]] = val
        else:
            n = REG16[idx & 3]
            v = self.r[n]
            self.r[n] = ((v & 0xFF) | (val << 8)) if idx & 4 else ((v & 0xFF00) | val)


def sx8(b):
    return b - 0x100 if b & 0x80 else b


def decode_rm(st, bs, i, override):
    modrm = bs[i]
    i += 1
    mod, reg, rm = modrm >> 6, (modrm >> 3) & 7, modrm & 7
    if mod == 3:
        return ('reg', rm), reg, i
    r = st.r
    if mod == 0 and rm == 6:
        ea, seg = bs[i] | (bs[i + 1] << 8), 'ds'
        i += 2
    else:
        base = [r['bx'] + r['si'], r['bx'] + r['di'], r['bp'] + r['si'], r['bp'] + r['di'],
                r['si'], r['di'], r['bp'], r['bx']][rm]
        disp = 0
        if mod == 1:
            disp = sx8(bs[i]); i += 1
        elif mod == 2:
            disp = bs[i] | (bs[i + 1] << 8); i += 2
        ea = (base + disp) & 0xFFFF
        seg = 'ss' if rm in (2, 3, 6) else 'ds'
    return ('mem', override or seg, ea), reg, i


def read(st, loc, w):
    if loc[0] == 'reg':
        return st.get(loc[1], w)
    return st.rd16(loc[1], loc[2]) if w == 16 else st.rd8(loc[1], loc[2])


def write(st, loc, w, v):
    if loc[0] == 'reg':
        st.put(loc[1], w, v)
    elif w == 16:
        st.wr16(loc[1], loc[2], v)
    else:
        st.wr8(loc[1], loc[2], v)


def flags_add_sub(f, a, b, r, w, is_sub):
    m, sb = (0xFF, 0x80) if w == 8 else (0xFFFF, 0x8000)
    rr = r & m
    cf = (a < b) if is_sub else (r > m)
    of = ((a ^ b) & (a ^ rr) & sb) if is_sub else ((a ^ rr) & (b ^ rr) & sb)
    for bit, on in ((C, cf), (O, of), (A, (a ^ b ^ rr) & 0x10), (Z, rr == 0), (S, rr & sb),
                    (P, bin(rr & 0xFF).count('1') % 2 == 0)):
        f = (f | bit) if on else (f & ~bit)
    return rr, f


stats = collections.Counter()


def load(name):
    # File decompressi da scripts/fetch_ssts.sh, oppure .json.gz.
    path = os.path.join(data_dir, name + '.json')
    return json.load(open(path)) if os.path.exists(path) else json.load(gzip.open(path + '.gz'))


def run(name):
    tests = load(name)
    res = collections.Counter()
    shown = 0
    for t in tests:
        st = State(t)
        bs = t['bytes']
        i, override, nprefix = 0, None, 0
        while bs[i] in PREFIX or bs[i] in (0xF0, 0xF2, 0xF3):
            if bs[i] in PREFIX:
                override = PREFIX[bs[i]]
            else:
                stats['altri prefissi'] += 1
            i += 1
            nprefix += 1
        stats[f'prefissi={nprefix}'] += 1
        op = bs[i]; i += 1
        w = 16 if op & 1 else 8
        tag = 'ok'
        if op in (0x88, 0x89, 0x8A, 0x8B, 0x01, 0x03, 0x29):
            loc, reg, i = decode_rm(st, bs, i, override)
            regloc = ('reg', reg)
            dst, src = (loc, regloc) if op in (0x88, 0x89, 0x01, 0x29) else (regloc, loc)
            if op in (0x88, 0x89, 0x8A, 0x8B):
                write(st, dst, w, read(st, src, w))
            else:
                a, b = read(st, dst, w), read(st, src, w)
                is_sub = op == 0x29
                r, st.r['flags'] = flags_add_sub(st.r['flags'], a, b, a - b if is_sub else a + b, w, is_sub)
                write(st, dst, w, r)
            if loc[0] == 'mem' and loc[2] == 0xFFFF and w == 16:
                tag = 'ok (parola a offset FFFF)'
        elif op in (0xA0, 0xA1, 0xA2, 0xA3):
            off = bs[i] | (bs[i + 1] << 8); i += 2
            loc = ('mem', override or 'ds', off)
            if op < 0xA2:
                write(st, ('reg', 0), w, read(st, loc, w))
            else:
                write(st, loc, w, read(st, ('reg', 0), w))
        elif op in (0x8C, 0x8E):
            loc, reg, i = decode_rm(st, bs, i, override)
            sreg = SEGS[reg & 3]
            if reg & 4:
                tag = 'reg 4-7 (alias di reg & 3)'
            if op == 0x8E and (reg & 3) == 1:
                tag = 'MOV CS' if not reg & 4 else tag
            if op == 0x8C:
                write(st, loc, 16, st.r[sreg])
            else:
                st.r[sreg] = read(st, loc, 16)
        else:
            continue
        st.r['ip'] = (t['initial']['regs']['ip'] + i) & 0xFFFF
        final = dict(t['initial']['regs'])
        final.update(t['final']['regs'])
        exp_ram = {a: v for a, v in t['final']['ram']}
        good = all(st.r[k] == final[k] for k in final if k != 'flags')
        good = good and ((st.r['flags'] ^ final['flags']) & 0x08D5) == 0
        good = good and all(exp_ram.get(a, st.mem.get(a)) == v for a, v in st.writes.items())
        good = good and all(st.mem.get(a) == v for a, v in exp_ram.items())
        res[(tag, good)] += 1
        if not good and shown < 2:
            shown += 1
            print(f'  FAIL {name} {t["name"]} bytes={bs} tag={tag}')
    print(f'{name}: ' + ', '.join(f'{k[0]}:{"PASS" if k[1] else "FAIL"}={v}' for k, v in sorted(res.items())))
    return res


totals = collections.Counter()
for n in sys.argv[2:]:
    totals.update(run(n))
print('statistiche prefissi:', dict(sorted(stats.items())))
print(f'totale PASS={sum(v for k, v in totals.items() if k[1])} '
      f'FAIL={sum(v for k, v in totals.items() if not k[1])}; per etichetta: '
      + ', '.join(f'{k[0]}:{"PASS" if k[1] else "FAIL"}={v}' for k, v in sorted(totals.items())))
