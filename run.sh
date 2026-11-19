#!/bin/sh
# Compiles the Blivzy AI engine on your PC with mock Flipper headers and runs sample questions.
# Usage: sh run.sh            (English)   |   sh run.sh hu   (Hungarian)
# Tests the answer engine only - the GUI must still be built with ./fbt on a Flipper SDK.
cd "$(dirname "$0")" || exit 1
gcc -std=gnu11 -Wall -Wextra -Wno-unused-function -Imock -I../../app/blivzy_ai -o /tmp/blivzy_harness harness.c -lm || exit 1
Q=${QF:-queries.txt}; [ "$1" = hu ] && [ -z "$QF" ] && Q=queries_hu.txt
SD="$(cd ../../sdcard && pwd)" /tmp/blivzy_harness "$@" < "$Q"
