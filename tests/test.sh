#!/bin/bash
# Fast correctness checks of the native solver, run by `make test`: deal sets
# against their known results, then each CLI mode against expected output
# (tests/*.expected) or, for random deals, against properties they must have.
# `tests/test.sh -u` rewrites tests/*.expected from the current solver.

cd "$(dirname "$0")/.."
solver=./solver
update=false
[[ $1 == -u ]] && update=true
failed=0

pass() { echo "ok - $1"; }
fail() { echo "FAIL - $1"; failed=$((failed + 1)); }

# Drops the time and memory columns from result lines.
strip_timing() { sed -E 's/ +[0-9.]+ s +[0-9.]+ M *$//'; }

# Runs a command and compares its output with tests/<name>.expected.
check() {
  local name=$1 expected=tests/$1.expected actual
  shift
  actual=$("$@" 2>/dev/null | strip_timing)
  if $update; then
    printf '%s\n' "$actual" > "$expected"
    echo "updated $expected"
  elif diff "$expected" <(printf '%s\n' "$actual") > /dev/null; then
    pass "$name"
  else
    fail "$name"
    diff "$expected" <(printf '%s\n' "$actual") | head -20
  fi
}

# Every deal in a directory, all strains and leaders, against its RESULTS,
# with run.sh (which prints the differences).
check_deals() {
  local output
  if output=$(./run.sh $1 2>&1); then
    pass "$1"
  else
    fail "$1"
    grep -v '^Results are in\|^Solved' <<< "$output" | head -20
  fi
}

# The cards in a deal's compact display (its first 3 lines), one per line
# as "<suit><rank>", sorted; with `skip`, the seats to leave out.
cards_of() {
  head -3 | awk -v skip="$1" '
    { seat = (NR == 1) ? "N" : (NR == 3) ? "S" : "W" }
    { for (i = 1; i <= NF; ++i) {
        if ($i == "\xe2\x99\xa0" || $i == "\xe2\x99\xa5" || $i == "\xe2\x99\xa6" || $i == "\xe2\x99\xa3") {
          if (NR == 2 && $i == "\xe2\x99\xa0" && suit != "") seat = "E"
          suit = $i
        } else if ($i != "-" && index(skip, seat) == 0) {
          for (j = 1; j <= length($i); ++j) print suit substr($i, j, 1)
        }
      }
      suit = ""
    }' | sort
}

# A result table: 5 strain lines, each with 4 trick counts.
is_table() {
  [[ $(grep -cE '^[NSHDC]( +[0-9]+){4}$' <<< "$1") == 5 ]]
}

if ! $update; then
  check_deals deals/old
  check_deals deals/fixed
fi

# A deal file's own strain and leader: each squeeze has South lead, and
# N/S take every trick.
for n in 1 2 3 4 5; do
  check squeeze.$n $solver -f deals/old/squeeze.$n -m0
done

check strain $solver -f deals/old/deal.1 -i -t S -m0
check display $solver -f deals/old/deal.1 -i -m 7 -o
check play $solver -f deals/old/squeeze.4 -p < <(printf '\n%.0s' $(seq 40))

if ! $update; then
  # -m 1 shows the deal's code, and -c solves it as the same deal.
  code=$($solver -f deals/old/deal.1 -i -m 1 -o | sed -n 's/^# //p')
  if [[ -n $code ]] &&
     diff <($solver -c "$code" -m0 | strip_timing) \
          <($solver -f deals/old/deal.1 -i -m0 | strip_timing) > /dev/null; then
    pass "code"
  else
    fail "code"
  fi

  # -s keeps the other seats' hands and shuffles the rest among the named ones.
  original=$($solver -f deals/old/deal.1 -i -m 2 -o)
  for i in 1 2 3; do
    shuffled=$($solver -f deals/old/deal.1 -i -s WE -d -m 2 | strip_timing)
    if [[ $(head -1 <<< "$shuffled") == $(head -1 <<< "$original") &&
          $(sed -n 3p <<< "$shuffled") == $(sed -n 3p <<< "$original") &&
          $(cards_of <<< "$shuffled") == $(cards_of <<< "$original") ]] &&
       is_table "$shuffled"; then
      pass "shuffle $i"
    else
      fail "shuffle $i"
    fi
  done

  # -r deals 52 distinct cards.
  for i in 1 2 3; do
    random=$($solver -r -m 2 | strip_timing)
    if [[ $(cards_of <<< "$random" | uniq | wc -l) -eq 52 ]] && is_table "$random"; then
      pass "random $i"
    else
      fail "random $i"
    fi
  done
fi

if ! $update; then
  [[ $failed == 0 ]] && echo "All passed." || echo "$failed failed."
fi
exit $((failed > 0))
