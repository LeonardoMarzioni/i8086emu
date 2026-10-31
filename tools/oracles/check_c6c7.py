# C6/C7 con reg != 0 si comportano come MOV r/m, imm (reg = 0)?
# - mod = 11: il registro destinazione finale deve valere l'immediato;
# - memoria: ogni cella di "final.ram" deve contenere un byte dell'immediato. Non si
#   controlla quale byte stia in quale cella, né l'indirizzo: se i due byte dell'immediato
#   di C7 sono uguali, il criterio non vede un byte alterato.
# Gli stessi criteri applicati a reg = 0 fanno da controllo: lì devono passare tutti.
# Con MUTA=1 l'immediato viene alterato: con reg 1-7 falliscono 17.481 test su 17.500, e i
# 19 che passano sono parole C7 con i due byte dell'immediato uguali (con reg 0: 1 su 2.500).
import collections, json, os, sys

REG16 = ['ax', 'cx', 'dx', 'bx', 'sp', 'bp', 'si', 'di']
PREFIXES = {0x26, 0x2E, 0x36, 0x3E}
d = sys.argv[1]
for name in ('C6', 'C7'):
    word = name == 'C7'
    res = collections.Counter()
    for t in json.load(open(os.path.join(d, name + '.json'))):
        b = t['bytes']
        i = 0
        while b[i] in PREFIXES:
            i += 1
        modrm = b[i + 1]
        mod, reg, rm = modrm >> 6, (modrm >> 3) & 7, modrm & 7
        disp = {0: 2 if rm == 6 else 0, 1: 1, 2: 2, 3: 0}[mod]
        p = i + 2 + disp
        imm = b[p] | (b[p + 1] << 8 if word else 0)
        if os.environ.get('MUTA'):  # controllo: con un immediato sbagliato deve fallire
            imm ^= 1
        regs = dict(t['initial']['regs'])
        regs.update(t['final']['regs'])
        kind = 'reg0' if reg == 0 else 'reg1-7'
        if mod == 3:
            if word:
                ok = regs[REG16[rm]] == imm
            else:
                v = regs[REG16[rm & 3]]
                ok = ((v >> 8) if rm & 4 else (v & 0xFF)) == imm
            res[(kind, 'registro', ok)] += 1
        else:
            vals = [v for _, v in t['final']['ram']]
            imm_bytes = [imm & 0xFF] + ([imm >> 8] if word else [])
            ok = all(v in imm_bytes for v in vals)
            res[(kind, 'memoria' if vals else 'memoria, nessuna cella cambiata', ok)] += 1
    for k, v in sorted(res.items()):
        print(name, k, v)
