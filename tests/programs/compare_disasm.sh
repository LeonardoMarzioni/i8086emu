#!/usr/bin/env bash
# Confronta il disassemblatore dell'emulatore con ndisasm, l'oracolo, sul codice di un
# programma di prova: da 0100h fino all'ultimo byte non nullo prima dei dati, che iniziano a
# 0200h. Le righe che iniziano dopo sono il riempimento di zeri (add [bx+si],al) e non vengono
# confrontate.
#
# Uso: compare_disasm.sh EMU PROGRAM.bin [NDISASM]
set -euo pipefail

emu=$1
program=$2
ndisasm=${3:-ndisasm}

# Indirizzo dell'ultimo byte non nullo della zona di codice (i primi 256 byte del file),
# nella stessa forma degli indirizzi qui sotto: quattro cifre esadecimali maiuscole.
last=$(head -c 256 "$program" | od -An -v -tu1 |
    awk '{ for (i = 1; i <= NF; i++) { if ($i != 0) last = n; n++ } } END { print last + 0 }')
code_end=$(printf '%04X' $((0x100 + last)))

# Gli awk leggono tutto l'input (next, non exit): uscendo prima chiuderebbero la pipe, e con
# pipefail il SIGPIPE di chi scrive farebbe terminare lo script senza alcun messaggio.

# ndisasm:  "00000100  BF0002            mov di,0x200"  ->  "0100 mov di,0x200"
expected=$("$ndisasm" -b 16 -o 0x100 "$program" |
    awk -v end="$code_end" '{ offset = substr($1, 5, 4) }
         offset > end { next }
         { $1 = ""; $2 = ""; sub(/^ +/, ""); print offset, $0 }')

# debugger: " > 1000:0100  BF0002         mov di,0x200"  ->  stessa forma
actual=$(printf 'disasm 256\nquit\n' | "$emu" "$program" | tail -n 256 |
    awk -v end="$code_end" '{ offset = substr($0, 9, 4) }
         offset > end { next }
         { print offset, substr($0, 30) }')

if [[ "$expected" != "$actual" ]]; then
    echo "disassembly of $program differs from ndisasm (< ndisasm, > i8086emu):"
    diff <(echo "$expected") <(echo "$actual") || true
    exit 1
fi
echo "$(echo "$expected" | wc -l) instructions identical to ndisasm (0100h-${code_end}h)"
