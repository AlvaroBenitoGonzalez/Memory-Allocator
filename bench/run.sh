#!/usr/bin/env bash
cd "$(dirname "$0")/.."

OUT=bench/results.out
: > "$OUT"

for mode in 0 1; do
    for n in 1 2 4 8; do
        echo "myalloc: $(./bench/b_myalloc "$n" "$mode")" >> "$OUT"
        echo "malloc:  $(./bench/b_malloc  "$n" "$mode")" >> "$OUT"
    done
done

cat "$OUT"