#!/usr/bin/env bash
# prof.sh TEST [N] [CTERM ARGS] - a PROFILE_ECLOCK build of this tree's
# CTerm runs cbone TEST N on the ctspeed rig; prints CTerm's counters (E
# clock ticks, 709379 a second). Now bench.sh's `prof` (the old dev drive
# and its one emulator are gone): the same as
#   tests/stock/bench.sh prof TEST N [CTERM ARGS]
exec "$(dirname "$0")/bench.sh" prof "$@"
