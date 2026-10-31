# i8086emu

Emulatore e debugger da terminale di un sottoinsieme dell'Intel 8086, in C++20.
Progetto d'esame del corso "Architetture dei Calcolatori e Cloud Computing".

La scelta del sottoinsieme di istruzioni deriva da mie precedenti esperienze in ambito didattico
con l'assembly 8086. Ho lasciato fuori le interruzioni e le forme far di salti e chiamate per
tenere l'emulatore semplice. Le regole le ho ricavate dal manuale Intel del 1979 e, prima di
scriverne il codice, le ho confrontate con SingleStepTests, una suite registrata su un 8088
vero. Il motivo è che un manuale, per quanto sia la specifica, non garantisce di descrivere ogni
comportamento del chip: volevo un confronto con l'hardware reale.

## Cosa fa

- **Istruzioni**: MOV, ADD, SUB, CMP,
  AND, OR, XOR, INC, DEC, NEG, NOT, PUSH, POP, JMP, JZ/JE, JNZ/JNE, CALL, RET (solo near),
  NOP, HLT. Tutti i modi di indirizzamento a 16 bit e i prefissi di segment override.
- **Debugger** da terminale: step, next, continue, breakpoint, registri, flag, dump e modifica
  della memoria, disassemblatore.
- **Verifica**:
  - [SingleStepTests](https://github.com/SingleStepTests/8088): 1.340.000 test, 0 falliti;
  - 127 test unitari e di integrazione;
  - tre programmi di prova in assembly;
  - disassemblatore confrontato con ndisasm.

  Come rifare ogni verifica: [Riprodurre i risultati](#riprodurre-i-risultati).

## Requisiti

Sviluppato e provato su WSL 2 con Ubuntu 24.04.

- GCC 13 o successivo (oppure Clang 17 o successivo), CMake 3.25 o successivo, Ninja
- `curl` e `gzip` per scaricare SingleStepTests
- NASM, facoltativo: serve solo per riassemblare i programmi di prova e per confrontare il
  disassemblatore con ndisasm (i binari sono già nel repository)
- Python 3, facoltativo: serve solo per gli script di verifica in `tools/oracles/`

```bash
sudo apt install build-essential cmake ninja-build curl nasm
```

## Build e test

Configurazione, compilazione e test in un solo comando:

```bash
cmake --workflow --preset debug
```

Il preset `debug` compila con AddressSanitizer e UndefinedBehaviorSanitizer, il preset
`release` con le ottimizzazioni (`cmake --workflow --preset release`). CTest esegue:

- i test unitari (Catch2): memoria, registri, flag, ALU, decoder, CPU, disassemblatore,
  caricatore, debugger, e i tre programmi di prova eseguiti fino a HLT;
- una sessione del debugger confrontata con l'output atteso;
- se ndisasm è installato, il confronto del disassemblatore con ndisasm sui programmi di prova.

Per rilanciare solo i test dopo una modifica:

```bash
cmake --build --preset debug && ctest --preset debug
```

## Programmi di prova

In `programs/` ci sono i sorgenti NASM e i binari assemblati.

| Programma | Cosa fa | Risultati |
|---|---|---|
| `fibonacci` | F(0)..F(23) in un array di parole | 1000:0200, AX = F(23) |
| `sort` | bubble sort di 10 parole, con una subroutine | 1000:0200 |
| `strings` | lunghezza, copia in maiuscolo e copia rovesciata di una stringa | 1000:0200..025F |

I dati stanno sempre da 1000:0200 in poi, così si trovano senza un file di simboli
(`x 200 32` nel debugger). I programmi terminano con HLT e usano solo il sottoinsieme: con i
soli JZ e JNZ, il confronto "minore di" usa il bit di segno di una differenza.

Per riassemblarli e controllare che non usino le forme 83 /1, /4, /6:

```bash
scripts/assemble_programs.sh
git status programs/   # i binari non devono cambiare
```

## Uso del debugger

Il programma è un binario piatto in stile .COM: viene caricato a 1000:0100, con
CS = DS = ES = SS = 1000 e SP = FFFE. **Deve terminare con HLT**.

```bash
./build/debug/src/cli/i8086emu programs/sort.bin
```

Opzioni: `--segment HEX`, `--ip HEX`, `--sp HEX` per cambiare lo stato iniziale, e
`--limit N` per il numero massimo di istruzioni eseguite da `continue` e `next`
(predefinito 1000000).

| Comando | Effetto |
|---|---|
| `step [n]` (`s`) | esegue n istruzioni (1 se omesso) |
| `next` (`n`) | come step, ma una CALL viene eseguita fino al ritorno |
| `continue` (`c`, `run`) | esegue fino a breakpoint, HLT, opcode non implementato, Ctrl-C o limite |
| `break [indirizzo]` (`b`) | imposta un breakpoint; senza argomenti li elenca |
| `delete [n]` | cancella il breakpoint n, o tutti |
| `regs`, `flags` | mostrano registri e flag |
| `x indirizzo [n]` | mostra n byte di memoria (16 se omesso) |
| `set reg valore`, `set indirizzo byte...` | modifica un registro, un flag o la memoria |
| `disasm [n]` | disassembla n istruzioni da CS:IP (10 se omesso) |
| `load file`, `reset` | carica un programma, lo ricarica da capo |
| `help`, `quit` | aiuto, uscita |

Indirizzi e valori sono esadecimali (`0x` facoltativo), i conteggi decimali. Un indirizzo è
`seg:off` oppure `off`; il segmento può essere `es`/`cs`/`ss`/`ds` e l'offset un registro a
16 bit, ad esempio `x ss:sp 16`. I comandi si possono anche passare da un file:
`i8086emu programma.bin < comandi.txt`. Un esempio di sessione, con l'output atteso, è in
`tests/programs/fibonacci_session.*`.

## Riprodurre i risultati

Questa sezione spiega come rifare da zero tutti i numeri riportati nel resoconto d'esame: i
test di SingleStepTests, le regole verificate prima di scrivere il codice, i controlli provati
su dati sbagliati, i confronti con ndisasm e le dimensioni del codice. Ogni passo indica il
risultato atteso. I comandi si lanciano dalla radice del repository, nell'ordine indicato.

### 1. Compilazione e test

```bash
cmake --workflow --preset release
```

Alla fine CTest deve scrivere `100% tests passed, 0 tests failed out of 127`.

### 2. I dati di SingleStepTests

```bash
scripts/fetch_ssts.sh
```

Lo script scarica i file del sottoinsieme dal set 8088, versione 2, al commit
`aea84484abc79d09639d855b7b0ab32bc9e4dbeb`, e li decomprime in `data/ssts` (esclusa da git):
circa 350 MB da scaricare, 2,8 GB una volta decompressi. Righe attese:

- `135 test files in the subset`;
- una riga `ok` per ogni file scaricato;
- `C6.0 is in C6.json` e `C7.0 is in C7.json`: per C6 e C7 la suite ha un solo file;
- `missing F4 (not in the suite)`: la suite non ha test di HLT.

### 3. L'harness di SingleStepTests

```bash
./build/release/tests/ssts/ssts_runner data/ssts
```

Stampa una riga per file con i test superati, falliti e saltati. L'ultima riga attesa è:

```
total: 135 files, 1340000 tests, 1312461 passed, 0 failed, 27539 skipped
```

I saltati sono le forme che l'emulatore non implementa per scelta, perché il manuale le dichiara
"not used": 8C/8E con reg 4–7 (10.039 test) e C6/C7 con reg 1–7 (17.500); le righe di quei
file le elencano forma per forma. L'esecuzione richiede poco più di un minuto. Per eseguire
solo alcuni file basta elencarli (`ssts_runner data/ssts 8B 80.4`).

Con `--markdown` l'harness stampa invece la tabella completa dei risultati, file per file:

```bash
./build/release/tests/ssts/ssts_runner --markdown data/ssts
```

### 4. Il disassemblatore e ndisasm

Il codice dei tre programmi di prova, disassemblato dall'emulatore e da ndisasm, deve coincidere
istruzione per istruzione:

```bash
ctest --test-dir build/release -R disasm_vs_ndisasm -V | grep identical
```

Attese 12, 26 e 49 istruzioni identiche, 87 in tutto. Le 52 forme campione usate durante lo
sviluppo:

```bash
bash tools/oracles/ndisasm_check.sh build/release/src/cli/i8086emu
```

L'ultima riga attesa è `uguali: 52 su 52`.

### 5. I programmi di prova

```bash
scripts/assemble_programs.sh
git status --short programs/
```

`git status` non deve mostrare nulla: i binari riassemblati sono identici a quelli versionati.
Lo script fallisce se NASM ha scelto da solo una forma 83 /1, /4, /6.

### 6. Le regole verificate prima del codice

`fetch_82.sh` scarica in `data/ssts-82` i file dell'opcode 82, che non fa parte del
sottoinsieme.

```bash
python3 -I tools/oracles/check_alu.py data/ssts data/ssts/metadata.json 04 05 0C 0D 24 25 2C 2D 34 35 3C 3D 40 48 F6.3 F7.3 FE.0 FE.1
python3 -I tools/oracles/check_addressing.py data/ssts 88 89 8A 8B A0 A1 A2 A3 8C 8E 01 03 29
tools/oracles/fetch_82.sh
python3 -I tools/oracles/check_82.py data/ssts-82 82.0 82.1 82.4 82.5 82.6 82.7
python3 -I tools/oracles/check_flow.py data/ssts 50 53 54 55 58 5B 5C 5D 74 75 E8 E9 EB C3 FF.2 FF.4
```

| Script | Ultima riga attesa |
|---|---|
| `check_alu.py` | `totale ok=149951 fail=0 skip=30049` |
| `check_addressing.py` | `totale PASS=130000 FAIL=0` (con `reg 4-7 (alias di reg & 3):PASS=10039`) |
| `check_82.py` | `totale mod=11: 15115/15115` (82.1, 82.4 e 82.6 ne danno 7.499) |
| `check_flow.py` | `totale PASS=160000 FAIL=0` (con `CALL SP:PASS=328`) |

Totale: 455.066 test. Questi quattro script non sono stati provati su dati sbagliati: il loro
risultato dice che regole e suite concordano. La garanzia sul codice arriva dall'harness, che
invece è stato provato.

### 7. Le verifiche scritte per il resoconto

I bit riservati di FLAGS (circa un minuto):

```bash
python3 -I tools/oracles/check_reserved_flags.py data/ssts/*.json | tail -n 1
```

Attesa `totale {'F002/0000': 2114663}`: in tutti i 2.114.663 valori di FLAGS della suite,
stati iniziali e finali, i bit 1 e 12–15 valgono 1 e i bit 3 e 5 valgono 0.

I bordi del segmento (circa un minuto):

```bash
python3 -I tools/oracles/check_wrap.py data/ssts
```

Output atteso:

```
test: 1340000
PUSH/CALL con SP 0 o 1: 0
POP/RET con SP FFFFh: 1
istruzione a cavallo della fine di CS: 0
test con due o piu prefissi di segmento: 0
sanita: test con almeno un prefisso 494621, ip+len massimo FFF6h, SP minimo nei PUSH/CALL 2h
```

C6/C7 con reg 1–7 contro MOV, prima con i dati veri e poi con l'immediato alterato apposta:

```bash
python3 -I tools/oracles/check_c6c7.py data/ssts
MUTA=1 python3 -I tools/oracles/check_c6c7.py data/ssts
```

Con i dati veri tutte le righe finiscono con `True`; quelle di `reg1-7` sommano
6532 + 2258 + 6505 + 2205 = 17.500 test. Con `MUTA=1` restano `True` solo
`C7 ('reg1-7', 'memoria', True) 19` e `C7 ('reg0', 'memoria', True) 1`: falliscono 17.481 test
su 17.500. I 19 sono parole C7 con i due byte dell'immediato uguali, che il criterio sulla memoria
non distingue (il commento in testa allo script lo spiega).

L'harness provato su test alterati apposta:

```bash
python3 -I tools/oracles/mutate_ssts.py data/ssts /tmp/ssts-mut
./build/release/tests/ssts/ssts_runner /tmp/ssts-mut 00
```

Attesi `7 passed, 3 failed` e codice di uscita 1: un registro, una cella di memoria e un flag
alterati falliscono, ciascuno con la differenza esatta; un bit riservato di FLAGS alterato viene
ignorato.

### 8. Le dimensioni del codice

```bash
for d in src/core src/debugger src/cli tests/ssts tests/unit; do echo "$d $(cat $d/*.cpp $d/*.hpp 2>/dev/null | wc -l)"; done
```

Righe totali, commenti e righe vuote compresi: 1547, 844, 119, 460 e 2167.

### Corrispondenza con il resoconto

| Numero nel resoconto | Sezione | Passo |
|---|---|---|
| 1.340.000 test, 1.312.461 superati, 0 falliti, 27.539 saltati | V-C, VII | 3 |
| poco più di un minuto per la suite | VI-C, VII | 3 |
| 127 test di CTest | VI-E, VII | 1 |
| 87 istruzioni e 52 forme identiche a ndisasm | VI-G, VII | 4 |
| 455.066 test: 149.951, 130.000, 15.115, 160.000 (Tabella VII) | VI-B | 6 |
| 10.039 test di 8C/8E e 7.499 di 82 (Tabella V) | V-A | 6 |
| 10.000 PUSH SP, 10.000 POP SP, 328 CALL SP | IV-A | 3 (file 54, 5C) e 6 |
| 2.114.663 valori di FLAGS | II-C | 7 |
| SP minimo 2, un solo POP con SP = FFFFh, IP + lunghezza al massimo FFF6h, 494.621 test con un prefisso | IV-B | 7 |
| 17.500 test di C6/C7 coerenti, 17.481 falliti con l'immediato alterato | V-A, VI-D | 7 |
| 3 falliti e 7 superati con i test alterati | VI-D | 7 |
| righe di codice (Tabella IX) | VII | 8 |

### Che cosa non è nel repository

- Il manuale Intel, *The 8086 Family User's Manual* (1979), da cui vengono il modello e le pagine
  citate nel resoconto. Una copia scansionata è a
  <https://edge.edx.org/c4x/BITSPilani/EEE231/asset/8086_family_Users_Manual_1_.pdf>.
- I dati di SingleStepTests, che si scaricano ai passi 2 e 6.

## Struttura

```
src/core/        libreria emu_core: CPU, memoria, decoder, disassemblatore, caricatore (nessun I/O)
src/debugger/    libreria emu_debugger: comandi, breakpoint, esecuzione
src/cli/         eseguibile i8086emu: argomenti, prompt, Ctrl-C
tests/unit/      test unitari (Catch2)
tests/programs/  sessione della CLI e confronto con ndisasm
tests/ssts/      harness di SingleStepTests
programs/        programmi di prova (.asm e .bin)
scripts/         download di SingleStepTests, assemblaggio dei programmi
tools/oracles/   script che confrontano le regole con SingleStepTests e ndisasm
cmake/           opzioni di compilazione e dipendenze
```
