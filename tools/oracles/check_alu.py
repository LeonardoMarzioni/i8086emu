# Verifica usa-e-getta delle formule dei flag (le stesse di alu.cpp) contro SingleStepTests.
import gzip, json, sys, os

data_dir, meta_path = sys.argv[1], sys.argv[2]
meta = json.load(open(meta_path))


def load(name):
    # File decompressi da scripts/fetch_ssts.sh, oppure .json.gz.
    path = os.path.join(data_dir, name + '.json')
    return json.load(open(path)) if os.path.exists(path) else json.load(gzip.open(path + '.gz'))

C, P, A, Z, S, O = 0x001, 0x004, 0x010, 0x040, 0x080, 0x800
ARITH = C | P | A | Z | S | O
SEG_PREFIX = {0x26, 0x2E, 0x36, 0x3E}
REG16 = ['ax', 'cx', 'dx', 'bx', 'sp', 'bp', 'si', 'di']


def setf(f, bit, on):
    return (f | bit) if on else (f & ~bit)


def szp(f, r, w):
    sb = 0x80 if w == 8 else 0x8000
    f = setf(f, Z, r == 0)
    f = setf(f, S, r & sb)
    return setf(f, P, bin(r & 0xFF).count('1') % 2 == 0)


def add(a, b, w, f, cf=True):
    m, sb = (0xFF, 0x80) if w == 8 else (0xFFFF, 0x8000)
    full = a + b
    r = full & m
    if cf:
        f = setf(f, C, full > m)
    f = setf(f, O, (a ^ r) & (b ^ r) & sb)
    f = setf(f, A, (a ^ b ^ r) & 0x10)
    return r, szp(f, r, w)


def sub(a, b, w, f, cf=True):
    m, sb = (0xFF, 0x80) if w == 8 else (0xFFFF, 0x8000)
    r = (a - b) & m
    if cf:
        f = setf(f, C, a < b)
    f = setf(f, O, (a ^ b) & (a ^ r) & sb)
    f = setf(f, A, (a ^ b ^ r) & 0x10)
    return r, szp(f, r, w)


def logic(r, w, f):
    for bit in (C, O, A):
        f = setf(f, bit, False)
    return r, szp(f, r, w)


def alu(op, a, b, w, f):
    if op == 0: return add(a, b, w, f)
    if op == 1: return logic(a | b, w, f)
    if op == 4: return logic(a & b, w, f)
    if op in (5, 7): return sub(a, b, w, f)
    if op == 6: return logic(a ^ b, w, f)
    raise ValueError(op)


def get_reg(regs, idx, w):
    if w == 16:
        return regs[REG16[idx]]
    v = regs[REG16[idx & 3]]
    return (v >> 8) & 0xFF if idx & 4 else v & 0xFF


def put_reg(regs, idx, w, val):
    if w == 16:
        regs[REG16[idx]] = val
    else:
        name = REG16[idx & 3]
        v = regs[name]
        regs[name] = (v & 0x00FF) | (val << 8) if idx & 4 else (v & 0xFF00) | val


def run(name):
    op_hex = name.split('.')[0]
    entry = meta['opcodes'][op_hex]
    if '.' in name:
        entry = entry['reg'][name.split('.')[1]]
    fmask = entry.get('flags-mask', 0xFFFF)
    tests = load(name)
    ok = bad = skipped = 0
    shown = 0
    for t in tests:
        bs = list(t['bytes'])
        while bs and bs[0] in SEG_PREFIX:
            bs.pop(0)
        if bs and bs[0] in (0xF0, 0xF2, 0xF3):
            skipped += 1
            continue
        op = bs[0]
        regs = dict(t['initial']['regs'])
        f = regs['flags']
        if '.' in name:                       # gruppi: solo mod=11
            modrm = bs[1]
            if modrm >> 6 != 3:
                skipped += 1
                continue
            w = 16 if op & 1 else 8
            rm = modrm & 7
            a = get_reg(regs, rm, w)
            sub_op = name.split('.')[1]
            if op_hex in ('F6', 'F7'):        # NEG
                r, f = sub(0, a, w, f)
            elif sub_op == '0':               # INC
                r, f = add(a, 1, w, f, cf=False)
            else:                             # DEC
                r, f = sub(a, 1, w, f, cf=False)
            put_reg(regs, rm, w, r)
        elif op in (0x40, 0x48):
            a = regs['ax']
            r, f = (add(a, 1, 16, f, cf=False) if op == 0x40 else sub(a, 1, 16, f, cf=False))
            regs['ax'] = r
        else:                                 # ALU accumulatore-immediato
            w = 16 if op & 1 else 8
            b = bs[1] | (bs[2] << 8) if w == 16 else bs[1]
            a = get_reg(regs, 0, w)
            r, f = alu((op >> 3) & 7, a, b, w, f)
            if (op >> 3) & 7 != 7:
                put_reg(regs, 0, w, r)
        final = dict(t['initial']['regs'])
        final.update(t['final']['regs'])
        good = all(regs[k] == final[k] for k in REG16) and \
            ((f ^ final['flags']) & fmask & ARITH) == 0
        if good:
            ok += 1
        else:
            bad += 1
            if shown < 3:
                shown += 1
                print(f'  FAIL {t["name"]}: init={t["initial"]["regs"]} final={t["final"]["regs"]} '
                      f'ours_flags={f:04X}')
    print(f'{name:6} ok={ok:5} fail={bad:5} skip={skipped:5} flags-mask={fmask:04X}')
    return ok, bad, skipped


totals = [0, 0, 0]
for n in sys.argv[3:]:
    totals = [t + r for t, r in zip(totals, run(n))]
print(f'totale ok={totals[0]} fail={totals[1]} skip={totals[2]}')
