#!/bin/bash
# Like ../run.sh, but solves with the web build's wasm under Node.
# Usage: web/run.sh [test_dir] [simd|nosimd] [suffix]

test_dir=${1:-deals/fixed}
test_dir=${test_dir%/}  # remove trailing slash
variant=${2:-simd}
results=results.$(basename $test_dir).$variant
if [[ -n $3 ]]; then
  results=$results.$3
fi
echo Results are in $results
cat $test_dir/* > /dev/null  # bring files into cache

deals=$(ls $test_dir | grep -v '^RESULTS$')
num_deals=$(echo "$deals" | wc -l)

TIMEFORMAT="Solved $num_deals deals in %1R seconds"
time (
  ${EMSDK_NODE:-node} $(dirname $0)/run.js $variant $(for deal in $deals; do echo $test_dir/$deal; done) > $results
)

diff $test_dir/RESULTS <(cut -c1-13 $results)
