#!/bin/bash

if [[ $1 == '-r' ]]; then
  echo $3 $(./solver -fi $2/$3 -H0 | sed -e "s/[0-9]*\.[0-9]* [sM]//g")
  exit
fi

test_dir=${1:-fixed_deals}
test_dir=${test_dir%%/*}  # remove trailing slashes
parallelism=${2:-2}
results=results.$test_dir.$parallelism
echo Results are in $results
cat $test_dir/* > /dev/null  # bring files into cache

start=$(date +"%s.%N")
ls $test_dir -I RESULTS | \
  xargs -L 1 -P $parallelism ./parallel_run_tests.sh -r $test_dir > $results
finish=$(date +"%s.%N")

num_deals=$(ls $test_dir -I RESULTS | wc -l)
echo Solved $num_deals deals in $(echo "scale=1;($finish-$start)/1" | bc) seconds

diff <(sed -e "N;N;N;N;N;s/\n/ /g;s/  */ /g" $test_dir/RESULTS | sort) <(sort $results)
