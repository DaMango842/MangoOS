#!/usr/bin/env bash
# Full rebuild with extra -D defines, for A/B perf experiments and the
# FC_TRACE_EVAL equivalence oracle.
#   exp_build.sh "FC_TRACE_EVAL FC_TRACE_EVAL_VERBOSE" [FC_LEGACY_DOTLOOP]
set -e
export PATH="/opt/cross/bin:/ucrt64/bin:/usr/bin:/bin:$PATH"
cd /d/Project/MangoOS
DEF="$1"
BASE="-m32 -ffreestanding -fno-pic -fno-stack-protector -fno-builtin -O2 -std=c11 -I kernel32 -MMD -MP"
for d in $DEF; do BASE="$BASE -D$d"; done
echo "building with:$BASE"
make clean >/dev/null
make all CFLAGS="$BASE" >/tmp/build.log 2>&1 || true
if grep -qE "error:" /tmp/build.log; then
  grep -E "error:" /tmp/build.log | head -20
  echo "BUILD FAILED"; exit 1
fi
echo "build ok"
ls -l image/kernel.bin | awk '{print "kernel.bin", $5, "bytes"}'