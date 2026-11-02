#!/bin/sh
# Host checks of the watchface's settings (day-WK): runs build/face/facetest (built with -DPD_FACE) under the settings and checks its summary.
#   cd bot; OUT=facetest MAIN=tests/facetest.c ./build.sh -DPD_FACE; sh tests/settest.sh [games per stage]
cd "$(dirname "$0")/.."
T=build/face/facetest; N=${1:-6}; FAIL=0
[ -x $T ] || { echo "build $T first (see above)"; exit 1; }
chk() { if [ "$2" = ok ]; then echo "PASS  $1"; else echo "FAIL  $1 ($2)"; FAIL=1; fi; }
run() { out=$(env "$@" $T $GAMES 2>&1); rc=$?; echo "$out" > /tmp/settest_last.txt; return $rc; }
field() { grep "$1" /tmp/settest_last.txt | head -1; }
# 1. the defaults of the settings (god mode on, as the face was before the settings; the others are the page's defaults) play as before
GAMES=12 run D=1 GOD=1; rc=$?; r=ok; [ $rc = 0 ] || r="exit"; grep -q "won 12, died 0" /tmp/settest_last.txt || r="not 12/12"; chk "defaults, 12 games: all won" $r
# 2. a run starting at each stage: the start is right (depth, level, health, bag with 3 free places), and it plays on for some turns
for st in 5 6 10 11 15 16 20 21 25; do
  GAMES=$N run VARY=1 GOD=1 STAGE=$st STEPS=2500; rc=$?; r=ok
  [ $rc = 0 ] || r="exit $rc (crash or hang: $(grep -a HANG /tmp/settest_last.txt | head -1))"
  grep -q "bad starts 0, packs without 3 free places 0" /tmp/settest_last.txt || r="bad start: $(grep 'BAD START' /tmp/settest_last.txt | head -1)"
  turns=$(grep -o "game turns played [0-9]*" /tmp/settest_last.txt | grep -o "[0-9]*$"); [ "${turns:-0}" -gt $((N * 200)) ] || r="only $turns turns played"
  chk "start at floor $st: $N games, $turns turns played" "$r"
done
# 3. god mode: nobody dies in 40 games from the deepest start; the control (god off) does die
GAMES=40 run VARY=1 GOD=1 STAGE=25 STEPS=4000; r=ok; grep -q "died 0" /tmp/settest_last.txt || r="somebody died"; chk "god mode, 40 games from floor 25: nobody died" "$r"
GAMES=40 run VARY=1 GOD=0 STAGE=25 STEPS=4000; d=$(grep -o "died [0-9]*" /tmp/settest_last.txt | tail -1 | grep -o "[0-9]*"); r=ok; [ "${d:-0}" -gt 0 ] || r="nobody died without god mode?"; chk "god off, 40 games from floor 25: $d died (control)" "$r"
# 4. classes: only the ticked ones, never the same twice in a row (unless one), none ticked = all four
for m in 2 5 9 16; do
  GAMES=40 run VARY=1 GOD=1 CLS=$m STEPS=300; line=$(field "classes used"); r=ok
  set -- $(echo "$line" | sed 's/.*huntress) //; s/,.*//'); w=$1; ma=$2; ro=$3; hu=$4
  case $m in 2) [ "$w$ro$hu" = 000 ] && [ "$ma" = 40 ] || r="classes $w $ma $ro $hu";; 5) [ "$ma$hu" = 00 ] && [ $((w + ro)) = 40 ] || r="classes $w $ma $ro $hu";;
    9) [ "$ma$ro" = 00 ] && [ $((w + hu)) = 40 ] || r="classes $w $ma $ro $hu";; 16) [ "$w" -gt 0 ] && [ "$ma" -gt 0 ] && [ "$ro" -gt 0 ] && [ "$hu" -gt 0 ] || r="classes $w $ma $ro $hu";; esac
  grep -q "same class twice in a row 0;" /tmp/settest_last.txt || r="a class twice in a row"
  chk "classes mask $m: warrior $w mage $ma rogue $ro huntress $hu" "$r"
done
# 5. the other cheats still play (each off in turn, 4 games)
for e in FR_REGEN=1 FR_REGEN=5 FR_HUNGER=1 FR_HEAL=0 FR_UNBREAK=0 FR_LOOT=0; do
  GAMES=4 run VARY=1 GOD=1 $e STEPS=3000; rc=$?; r=ok; [ $rc = 0 ] || r="exit $rc"; chk "$e: 4 games play" "$r"
done
[ $FAIL = 0 ] && echo "ALL PASS" || echo "SOME FAILED"
exit $FAIL
