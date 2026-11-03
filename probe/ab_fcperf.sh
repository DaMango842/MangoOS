#!/usr/bin/env bash
# Interleaved A/B: alternate between the call-per-dot reference and the
# inlined-dispatch build, several rounds, so host-speed drift cancels out.
# Metric: the emulator's own frames-per-second counter (n), which is
# self-normalising and immune to the PIT wrap that corrupts the cpu/ppu/rnd
# split.
set -e
export PATH="/opt/cross/bin:/ucrt64/bin:/usr/bin:/bin:$PATH"
cd /d/Project/MangoOS
ROUNDS=${1:-2}
SECS=${2:-45}
TAG=${3:-ab}

for i in $(seq 1 "$ROUNDS"); do
  for cfg in MONO SPLIT; do
    if [ "$cfg" = "MONO" ]; then
      DEF="FC_LEGACY_DOTLOOP"
      NAME="call-per-dot(before)"
    else
      DEF=""
      NAME="inlined-dispatch(after)"
    fi
    bash probe/exp_build.sh "$DEF" > /tmp/b.log 2>&1
    if grep -q "BUILD FAILED" /tmp/b.log; then echo "build failed: $cfg"; exit 1; fi
    echo "=== round $i  $NAME ==="
    PY=$(command -v python || command -v python3 || echo /d/Softwares/Python313/python.exe)
    "$PY" probe/probe_fcperf.py "$NAME" "$SECS" \
      | grep -E "TRUE fps|frame total|cpu |ppu " || true
  done
done