#!/usr/bin/env bash
# Assembla i programmi di prova in binari piatti (.bin), che sono versionati nel repository.
#
# Controlla anche che NASM non abbia scelto da solo le forme 83 /1, /4, /6 (OR, AND, XOR
# con immediato a 8 bit esteso), che la Tabella 4-13 del manuale indica "not used": NASM le
# usa per un registro a 16 bit con un immediato tra -128 e 127.
#
# Uso: scripts/assemble_programs.sh   (dalla radice del repository)
set -euo pipefail

status=0
for source in programs/*.asm; do
    binary=${source%.asm}.bin
    nasm -f bin -o "$binary" "$source"
    if ndisasm -b 16 -o 0x100 "$binary" | grep -E '^0000(01|00)' | grep -qE '\s(and|or|xor) [a-z]+,byte [+-]'; then
        echo "error: $source uses an 83 /1, /4 or /6 form (use 'strict word')" >&2
        status=1
    fi
    echo "$binary: $(stat -c %s "$binary") bytes"
done
exit $status
