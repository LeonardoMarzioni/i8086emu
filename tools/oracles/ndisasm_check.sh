#!/usr/bin/env bash
# Confronto usa-e-getta: il disassemblatore dell'emulatore contro ndisasm sugli stessi byte.
set -euo pipefail
emu=$1
work=$(mktemp -d)
cases=(
  "B8 34 12" "B0 05" "88 C4" "8A E0" "81 C3 00 01" "04 FF" "31 C0" "83 C0 FF" "83 E3 0F"
  "40" "FE C4" "F6 D8" "F7 D3" "8C D8" "8E D0"
  "8B 00" "8B 46 FE" "8B 46 00" "8B 87 34 12" "8B 06 34 12" "89 07" "A1 34 12" "A2 00 01"
  "8E 1E 34 12" "C6 07 42" "C7 47 02 34 12" "80 3F 05" "83 7E FC 05" "FE 07" "F7 1F" "01 07"
  "26 8B 07" "2E A1 00 01" "3E 8B 46 00" "26 B8 34 12"
  "EB FE" "E9 FD FF" "74 05" "75 F0" "E8 00 00" "FF D3" "FF 17" "FF 67 02" "C3" "50" "5F" "90" "F4"
  "8B 87 FE FF" "8B 86 00 80" "81 2E 34 12 78 56" "83 C4 80"
)
hex=$(printf '%s ' "${cases[@]}")
python3 -I -c "import sys; sys.stdout.buffer.write(bytes.fromhex(sys.argv[1]))" "$hex" > "$work/all.bin"
n=${#cases[@]}

ndisasm -b 16 -o 0x100 "$work/all.bin" | awk '{ $1 = ""; $2 = ""; sub(/^ +/, ""); print }' > "$work/ndisasm.txt"
printf 'disasm %d\nquit\n' "$n" | "$emu" "$work/all.bin" | tail -n "$n" | cut -c30- > "$work/emu.txt"

paste -d '|' "$work/ndisasm.txt" "$work/emu.txt" | awk -F'|' '{ status = ($1 == $2) ? "ok  " : "DIFF"; printf "%s %-32s %s\n", status, $1, $2 }'
echo "uguali: $(paste -d '|' "$work/ndisasm.txt" "$work/emu.txt" | awk -F'|' '$1 == $2' | wc -l) su $n"
rm -r "$work"
