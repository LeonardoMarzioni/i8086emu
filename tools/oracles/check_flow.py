# Verifica usa-e-getta di PUSH/POP, salti, CALL/RET contro SingleStepTests.
# Le regole qui sotto sono le stesse implementate nel codice C++.
import collections, gzip, json, os, sys

data_dir = sys.argv[1]
REG16 = ['ax', 'cx', 'dx', 'bx', 'sp', 'bp', 'si', 'di']
PREFIX = {0x26: 'es', 0x2E: 'cs', 0x36: 'ss', 0x3E: 'ds'}
ZF = 0x40


def phys(seg, off):
    return ((seg << 4) + (off & 0xFFFF)) & 0xFFFFF


def sx8(b):
    return b - 0x100 if b & 0x80 else b


class State:
    def __init__(self, t):
        self.r = dict(t['initial']['regs'])
        self.mem = {a: v for a, v in t['initial']['ram']}

    def rd16(self, seg, off):
        return self.mem[phys(self.r[seg], off)] | (self.mem[phys(self.r[seg], off + 1)] << 8)

    def wr16(self, seg, off, v):
        self.mem[phys(self.r[seg], off)] = v & 0xFF
        self.mem[phys(self.r[seg], off + 1)] = v >> 8

    # PUSH: prima si decrementa SP, poi si scrive. Il valore viene letto dal chiamante.
    def push(self, value_fn):
        self.r['sp'] = (self.r['sp'] - 2) & 0xFFFF
        self.wr16('ss', self.r['sp'], value_fn())

    def pop(self):
        v = self.rd16('ss', self.r['sp'])
        self.r['sp'] = (self.r['sp'] + 2) & 0xFFFF
        return v


def rm_word(st, bs, i, override):
    modrm = bs[i]
    i += 1
    mod, reg, rm = modrm >> 6, (modrm >> 3) & 7, modrm & 7
    if mod == 3:
        return st.r[REG16[rm]], reg, i
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
    return st.rd16(override or seg, ea), reg, i


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
        i, override = 0, None
        while bs[i] in PREFIX:
            override = PREFIX[bs[i]]
            i += 1
        op = bs[i]
        i += 1
        tag = 'ok'
        ip0 = t['initial']['regs']['ip']
        if 0x50 <= op <= 0x57:
            st.push(lambda: st.r[REG16[op & 7]])   # PUSH SP legge SP già decrementato
            ip = ip0 + i
            tag = 'PUSH SP' if op == 0x54 else tag
        elif 0x58 <= op <= 0x5F:
            v = st.pop()
            st.r[REG16[op & 7]] = v                # POP SP: vince il valore letto
            ip = ip0 + i
            tag = 'POP SP' if op == 0x5C else tag
        elif op in (0x74, 0x75, 0xEB):
            rel = sx8(bs[i]); i += 1
            nxt = ip0 + i
            taken = op == 0xEB or ((st.r['flags'] & ZF) != 0) == (op == 0x74)
            ip = nxt + rel if taken else nxt
            if op != 0xEB:
                tag = 'preso' if taken else 'non preso'
        elif op in (0xE8, 0xE9):
            rel = bs[i] | (bs[i + 1] << 8); i += 2
            nxt = (ip0 + i) & 0xFFFF
            if op == 0xE8:
                st.push(lambda: nxt)
            ip = nxt + rel
        elif op == 0xC3:
            ip = st.pop()
        elif op == 0xFF:
            modrm = bs[i]
            target, reg, i = rm_word(st, bs, i, override)  # il bersaglio si legge PRIMA del push
            nxt = (ip0 + i) & 0xFFFF
            if reg == 2:
                st.push(lambda: nxt)
                tag = 'CALL SP' if modrm == 0xD4 else ('CALL reg' if modrm >> 6 == 3 else 'CALL mem')
            else:
                tag = 'JMP reg' if modrm >> 6 == 3 else 'JMP mem'
            ip = target
        else:
            continue
        st.r['ip'] = ip & 0xFFFF
        final = dict(t['initial']['regs'])
        final.update(t['final']['regs'])
        exp_ram = {a: v for a, v in t['final']['ram']}
        good = all(st.r[k] == final[k] for k in final if k != 'flags')
        good = good and all(st.mem.get(a) == v for a, v in exp_ram.items())
        res[(tag, good)] += 1
        if not good and shown < 2:
            shown += 1
            print(f'  FAIL {name} bytes={bs} init={t["initial"]["regs"]} final={t["final"]["regs"]} ours_ip={st.r["ip"]:04X} ours_sp={st.r["sp"]:04X}')
    print(f'{name}: ' + ', '.join(f'{k[0]}:{"PASS" if k[1] else "FAIL"}={v}' for k, v in sorted(res.items())))
    return res


totals = collections.Counter()
for n in sys.argv[2:]:
    totals.update(run(n))
print(f'totale PASS={sum(v for k, v in totals.items() if k[1])} '
      f'FAIL={sum(v for k, v in totals.items() if not k[1])}; per etichetta: '
      + ', '.join(f'{k[0]}:{"PASS" if k[1] else "FAIL"}={v}' for k, v in sorted(totals.items())))
