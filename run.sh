#!/bin/bash

test_dir=${1:-deals/fixed}
test_dir=${test_dir%/}  # remove trailing slash
results=results.$(basename $test_dir)
if [[ -n $2 ]]; then
  results=$results.$2
fi
echo Results are in $results
cat $test_dir/* > /dev/null  # bring files into cache

deals=$(ls $test_dir | grep -v '^RESULTS$')
num_deals=$(echo "$deals" | wc -l)

TIMEFORMAT="Solved $num_deals deals in %1R seconds"
time (
  for deal in $deals; do
    echo $deal
    ./solver -if $test_dir/$deal -m0
  done > $results
)

diff $test_dir/RESULTS <(cut -c1-13 $results)
