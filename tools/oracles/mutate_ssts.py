# Altera apposta alcuni test per verificare che l'harness rilevi gli errori.
# Uso: mutate_ssts.py SRC_DIR DST_DIR  (copia metadata.json e un 00.json alterato)
import json, os, shutil, sys

src, dst = sys.argv[1], sys.argv[2]
os.makedirs(dst, exist_ok=True)
shutil.copy(os.path.join(src, 'metadata.json'), dst)
tests = json.load(open(os.path.join(src, '00.json')))

def final_value(t, reg):
    return t['final']['regs'].get(reg, t['initial']['regs'][reg])

# 0: registro atteso sbagliato -> deve fallire
t = tests[0]; t['final']['regs']['ax'] = final_value(t, 'ax') ^ 1
# 1: cella di memoria attesa sbagliata -> deve fallire
t = tests[1]; t['final']['ram'].append([t['initial']['ram'][0][0], t['initial']['ram'][0][1] ^ 0xFF])
# 2: flag definito sbagliato (CF) -> deve fallire
t = tests[2]; t['final']['regs']['flags'] = final_value(t, 'flags') ^ 0x0001
# 3: bit riservato di FLAGS (bit 15) -> deve passare
t = tests[3]; t['final']['regs']['flags'] = final_value(t, 'flags') ^ 0x8000

json.dump(tests[:10], open(os.path.join(dst, '00.json'), 'w'))
print('scritti 10 test, attesi 3 falliti e 7 superati')
