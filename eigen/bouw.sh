#!/bin/zsh
# Tekstuur voor de FM-1 bouwen met fm1-base.
#   ./bouw.sh host      de simulator op de Mac (en de tests in tests/)
#   ./bouw.sh device    de firmware: build/tekstuur.fwsc
# fm1-base staat in $FM1_BASE (standaard ~/Desktop/fm1/ref/fm1-base). De map van dit project heeft een spatie in haar
# pad, wat de bouwscripts niet verdragen; daarom wordt de app eerst naar $FM1_BASE/tekstuur gekopieerd.
set -e
HERE=${0:A:h}
BASE=${FM1_BASE:-$HOME/Desktop/fm1/ref/fm1-base}
PY=${FM1_PY:-$HOME/Desktop/fm1/venv/bin/python}
export JIELI_TOOLCHAIN=${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain} AC79_SDK=${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}
rsync -a --delete --exclude build --exclude '.git*' "$HERE/" "$BASE/tekstuur/"
mkdir -p "$BASE/tekstuur/build" "$HERE/build"
cd "$BASE"
if [[ $1 == host ]]; then
    $PY tools/build.py tekstuur --host
    for t in tekstuur/tests/*.txt; do
        out="$HERE/build/host/${t:t:r}"
        mkdir -p "$out"
        echo "== ${t:t}"
        tekstuur/build/host/tekstuur_host "$t" "$out"
    done
else
    $PY tools/build.py tekstuur ${@:2}
    cp "$(ls -t tekstuur/build/*.fwsc | head -1)" "$HERE/build/tekstuur.fwsc"
    ls -l "$HERE/build/tekstuur.fwsc"
fi
