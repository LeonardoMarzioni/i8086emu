#!/usr/bin/env bash
# Scarica i sei file di SingleStepTests dell'opcode 82 che servono a check_82.py (82.0, 82.1,
# 82.4, 82.5, 82.6, 82.7), dallo stesso commit di scripts/fetch_ssts.sh. 82 /1, /4, /6 non
# fanno parte del sottoinsieme, quindi fetch_ssts.sh non li scarica.
#
# I file vanno in una cartella a parte: check_wrap.py legge tutti i file di data/ssts, e un
# file in più cambierebbe i suoi conteggi.
#
# Uso: tools/oracles/fetch_82.sh [DATA_DIR]   (predefinito: data/ssts-82)
set -euo pipefail
readonly COMMIT=aea84484abc79d09639d855b7b0ab32bc9e4dbeb
readonly BASE_URL="https://raw.githubusercontent.com/SingleStepTests/8088/${COMMIT}/v2"
data_dir=${1:-data/ssts-82}
mkdir -p "$data_dir"
for reg in 0 1 4 5 6 7; do
    name=82.$reg
    target="$data_dir/$name.json"
    if [[ -s "$target" ]]; then
        continue  # già scaricato
    fi
    curl -fsSL "$BASE_URL/$name.json.gz" | gunzip -c > "$target.tmp"
    mv "$target.tmp" "$target"
    echo "ok      $name"
done
