#!/bin/bash

if [[ $1 == '-r' ]]; then
  echo $3 $(./solver -if $2/$3 -m0 | sed -e "s/[0-9]*\.[0-9]* [sM]//g")
  exit
fi

test_dir=${1:-deals/fixed}
test_dir=${test_dir%/}  # remove trailing slash
parallelism=${2:-2}
results=results.$(basename $test_dir).$parallelism
echo Results are in $results
cat $test_dir/* > /dev/null  # bring files into cache

deals=$(ls $test_dir | grep -v '^RESULTS$')
num_deals=$(echo "$deals" | wc -l)

TIMEFORMAT="Solved $num_deals deals in %1R seconds"
time (echo "$deals" | xargs -L 1 -P $parallelism ./parallel_run.sh -r $test_dir > $results)

diff <(sed -e "N;N;N;N;N;s/\n/ /g;s/  */ /g" $test_dir/RESULTS | sort) <(sort $results)
