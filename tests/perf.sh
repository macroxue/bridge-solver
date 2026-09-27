#!/bin/bash
# Benchmarks the native solver, run by `make perf`: each deal set with run.sh
# (which also checks its RESULTS), pinned to one CPU and with transparent huge
# pages through glibc, as in README.md's Performance section. Takes minutes.
# Usage: tests/perf.sh [CPU] [DIRECTORY...]

cd "$(dirname "$0")/.."
cpu=${1:-0}
[[ $# -gt 0 ]] && shift
[[ $# -eq 0 ]] && set -- deals/old deals/fixed deals/new deals/hard deals/long deals/1k deals/freak

# Huge pages need glibc 2.35+ and THP set to madvise or always.
export GLIBC_TUNABLES=glibc.malloc.hugetlb=1
thp=/sys/kernel/mm/transparent_hugepage/enabled
if [[ -r $thp ]]; then
  echo "THP: $(cat $thp)"
  grep -q '\[never\]' $thp && echo "Warning: THP is off, so no huge pages." >&2
fi
if command -v taskset > /dev/null; then
  pin="taskset -c $cpu"
  echo "Pinned to CPU $cpu."
else
  pin=
  echo "Warning: no taskset, so runs aren't pinned to a CPU." >&2
fi
# Results go to results.<set>.<commit>, e.g. results.1k.6709d7, with -dirty
# when tracked files have uncommitted changes.
suffix=$(git rev-parse --short=6 HEAD)
git diff --quiet HEAD || suffix=$suffix-dirty
echo "Solver at $suffix."

summary=()
failed=0
for dir in "$@"; do
  dir=${dir%/}
  echo
  echo "== $dir"
  output=$($pin ./run.sh $dir $suffix 2>&1)
  status=$?
  echo "$output"
  line=$(grep '^Solved' <<< "$output")
  if [[ $status -eq 0 ]]; then
    summary+=("$dir: $line")
  else
    summary+=("$dir: $line, WRONG RESULTS")
    failed=$((failed + 1))
  fi
done

echo
echo "== Summary"
printf '%s\n' "${summary[@]}"
exit $((failed > 0))
