#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Starboard Protocol — MISRA C:2012 check of the C99 flight library.
# Copyright (C) 2026 James H. All rights reserved.
#
# Runs cppcheck's MISRA C:2012 addon plus its general static analysis over
# every flight-library source, and prints a per-rule summary. Each remaining
# rule is either fixed or a DOCUMENTED deviation (docs/evidence/MISRA.md);
# the script fails if a rule appears that is not on the deviation list, or
# if cppcheck reports a non-style defect.
#
#   tools/misra.sh            # summary
#   tools/misra.sh --full     # every finding
set -u
cd "$(dirname "$0")/../ports/c"
FILES="canon.c geometry.c propagator.c covariance.c screening.c rules.c
       maneuver.c multibody.c broadphase.c sha512.c registry.c ed25519.c
       cdm.c fsw.c ccsds.c flock.c softdouble.c"
OUT=$(mktemp)
trap 'rm -f "$OUT"' EXIT
cppcheck --std=c99 --addon=misra --enable=warning,style,portability,performance \
    --inline-suppr -q --template='{file}:{line}:{id}:{message}' -I. $FILES \
    2> "$OUT"
# Deviations recorded in docs/evidence/MISRA.md (rule -> rationale there).
DEVIATED="2.3 2.5 8.4 8.7 8.9 9.3 10.1 10.4 10.6 10.7 10.8 11.5 12.2 12.3 14.1 14.2
          15.4 15.5 17.8 18.4 21.15"
total=$(grep -c "misra-c2012" "$OUT")
echo "MISRA C:2012 (cppcheck $(cppcheck --version | awk '{print $2}')):" \
     "$total findings across $(echo $FILES | wc -w) files"
grep -o "misra-c2012-[0-9.]*" "$OUT" | sort | uniq -c | sort -rn |
    awk '{printf "  rule %-6s %4d\n", substr($2, 13), $1}'
bad=0
for r in $(grep -o "misra-c2012-[0-9.]*" "$OUT" | sort -u | sed 's/misra-c2012-//'); do
    case " $(echo $DEVIATED) " in
        *" $r "*) ;;
        *) echo "UNDEVIATED rule $r:"; grep "misra-c2012-$r:" "$OUT" | head -5; bad=1 ;;
    esac
done
other=$(grep -v "misra-c2012" "$OUT" | grep -vE ":(variableScope|constParameterPointer|constVariablePointer|objectIndex|unusedStructMember|knownConditionTrueFalse):" )
if [ -n "$other" ]; then
    echo "non-MISRA cppcheck defects:"; echo "$other"; bad=1
fi
[ "${1:-}" = "--full" ] && cat "$OUT"
[ $bad -eq 0 ] && echo "misra: every finding is fixed or a documented deviation"
exit $bad
