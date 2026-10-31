#!/usr/bin/env bash
# Scarica da un commit fissato i file di SingleStepTests (8088, v2) che servono al
# sottoinsieme dell'emulatore, e li decomprime nella cartella dei dati.
#
# L'elenco dei file lo fornisce l'harness (ssts_runner --list), che lo ricava dal decoder:
# per questo va compilato prima di lanciare lo script.
#
# Uso: scripts/fetch_ssts.sh [RUNNER] [DATA_DIR]
#   RUNNER    predefinito: build/release/tests/ssts/ssts_runner
#   DATA_DIR  predefinito: data/ssts (esclusa da git)
set -euo pipefail

readonly COMMIT=aea84484abc79d09639d855b7b0ab32bc9e4dbeb
readonly BASE_URL="https://raw.githubusercontent.com/SingleStepTests/8088/${COMMIT}/v2"

runner=${1:-build/release/tests/ssts/ssts_runner}
data_dir=${2:-data/ssts}

if [[ ! -x "$runner" ]]; then
    echo "harness not found: $runner (build it first: cmake --workflow --preset release)" >&2
    exit 1
fi

mkdir -p "$data_dir"
curl -fsSL "$BASE_URL/metadata.json" -o "$data_dir/metadata.json"
echo "$COMMIT" > "$data_dir/SOURCE"

mapfile -t files < <("$runner" --list "$data_dir/metadata.json")
echo "${#files[@]} test files in the subset"

# Scarica e decomprime un file; restituisce 1 se la suite non lo contiene. Si decomprime in un
# file temporaneo e lo si rinomina solo alla fine: un'interruzione non lascia un file a metà
# che al giro successivo sembrerebbe completo.
fetch() {
    local name=$1
    local target="$data_dir/$name.json"
    if [[ -s "$target" ]]; then
        return 0  # già scaricato
    fi
    if ! curl -fsSL "$BASE_URL/$name.json.gz" -o "$target.gz" 2>/dev/null; then
        rm -f "$target.gz"
        return 1
    fi
    gunzip -c "$target.gz" > "$target.tmp"
    mv "$target.tmp" "$target"
    rm "$target.gz"
    echo "ok      $name"
}

for name in "${files[@]}"; do
    fetch "$name" && continue
    # Per alcuni opcode di gruppo (C6, C7) la suite ha un solo file per l'intero opcode,
    # anche se metadata.json li divide per campo reg.
    if [[ "$name" == *.* ]] && fetch "${name%%.*}"; then
        echo "        $name is in ${name%%.*}.json"
        continue
    fi
    echo "missing $name (not in the suite)"
done
