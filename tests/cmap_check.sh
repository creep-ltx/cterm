#!/usr/bin/env bash
# cmap_check.sh FONT.ttf... - CTerm's cmap reader (tests/cmap_test)
# against fontconfig's (fc-query): the same characters for each font.
# fontconfig leaves out the C0/C1 controls a font maps; so does this.
cd "$(dirname "$0")/.." || exit 2
bad=0
for f in "$@"; do
    tests/cmap_test "$f" > /tmp/cmap_ours.txt || { echo "$f: cmap_test failed"; bad=1; continue; }
    fc-query --format '%{charset}\n' "$f" | head -1 > /tmp/cmap_fc.txt
    if python3 -I tests/cmap_cmp.py /tmp/cmap_ours.txt /tmp/cmap_fc.txt; then
        echo "$f: the same as fontconfig"
    else
        echo "$f: DIFFERENT"
        bad=1
    fi
done
exit $bad
