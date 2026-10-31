# Conta nella suite i test che toccano i bordi del segmento:
# - stack: PUSH/CALL con SP iniziale 0 o 1 (la parola scritta passa per la fine di SS),
#          POP/RET con SP iniziale FFFFh (la parola letta passa per la fine di SS);
# - istruzione a cavallo della fine di CS: ip + numero di byte > 10000h;
# - prefissi: test con piu' di un prefisso di segmento all'inizio.
import json, os, sys

d = sys.argv[1]
push_files = [f'{x:02X}' for x in range(0x50, 0x58)] + ['E8', 'FF.2']
pop_files = [f'{x:02X}' for x in range(0x58, 0x60)] + ['C3']
prefixes = {0x26, 0x2E, 0x36, 0x3E}
stack_push = stack_pop = cross_cs = multi_prefix = total = one_prefix = max_end = 0
min_sp_push = 0x10000
for name in sorted(os.listdir(d)):
    if not name.endswith('.json') or name == 'metadata.json':
        continue
    stem = name[:-5]
    for t in json.load(open(os.path.join(d, name))):
        total += 1
        r = t['initial']['regs']
        if stem in push_files and r['sp'] in (0, 1):
            stack_push += 1
        if stem in pop_files and r['sp'] == 0xFFFF:
            stack_pop += 1
        if r['ip'] + len(t['bytes']) > 0x10000:
            cross_cs += 1
        b = t['bytes']
        if len(b) > 1 and b[0] in prefixes and b[1] in prefixes:
            multi_prefix += 1
        # controlli di sanità: i criteri devono saper trovare qualcosa
        if b[0] in prefixes:
            one_prefix += 1
        max_end = max(max_end, r['ip'] + len(b))
        if stem in push_files:
            min_sp_push = min(min_sp_push, r['sp'])
print(f'test: {total}')
print(f'PUSH/CALL con SP 0 o 1: {stack_push}')
print(f'POP/RET con SP FFFFh: {stack_pop}')
print(f'istruzione a cavallo della fine di CS: {cross_cs}')
print(f'test con due o piu prefissi di segmento: {multi_prefix}')
print(f'sanita: test con almeno un prefisso {one_prefix}, ip+len massimo {max_end:X}h, '
      f'SP minimo nei PUSH/CALL {min_sp_push:X}h')
