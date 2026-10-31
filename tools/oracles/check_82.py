# Verifica usa-e-getta: l'opcode 82 si comporta come 80 per ogni valore del campo reg?
import gzip, json, os, sys

data_dir = sys.argv[1]
REG = ['ax', 'cx', 'dx', 'bx']


def load(name):
    # File decompressi (tools/oracles/fetch_82.sh), oppure .json.gz.
    path = os.path.join(data_dir, name + '.json')
    return json.load(open(path)) if os.path.exists(path) else json.load(gzip.open(path + '.gz'))


total_ok = total_n = 0
for name in sys.argv[2:]:
    op = int(name.split('.')[1])
    ok = n = 0
    for t in load(name):
        bs = t['bytes']
        i = 0
        while bs[i] in (0x26, 0x2E, 0x36, 0x3E):
            i += 1
        modrm = bs[i + 1]
        if modrm >> 6 != 3:
            continue
        n += 1
        reg_index = modrm & 7
        imm = bs[i + 2]
        regs = t['initial']['regs']
        v = regs[REG[reg_index & 3]]
        a = (v >> 8) if reg_index & 4 else v & 0xFF
        res = {0: a + imm, 1: a | imm, 4: a & imm, 5: a - imm, 6: a ^ imm, 7: a - imm}[op] & 0xFF
        if op != 7:
            v = ((v & 0xFF) | (res << 8)) if reg_index & 4 else ((v & 0xFF00) | res)
        final = dict(regs)
        final.update(t['final']['regs'])
        ok += final[REG[reg_index & 3]] == v
    print(f'{name} mod=11: {ok}/{n} con il risultato di 80 /{op}')
    total_ok += ok
    total_n += n
print(f'totale mod=11: {total_ok}/{total_n}')
