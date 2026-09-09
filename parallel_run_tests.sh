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

list_deals() {
  # Portable replacement for GNU `ls -I RESULTS`.
  ls "$1" | grep -v '^RESULTS$'
}

start=$(date +"%s.%N")
list_deals "$test_dir" | \
  xargs -L 1 -P $parallelism ./parallel_run_tests.sh -r $test_dir > $results
finish=$(date +"%s.%N")

num_deals=$(list_deals "$test_dir" | wc -l)
echo Solved $num_deals deals in $(echo "scale=1;($finish-$start)/1" | bc) seconds

diff <(sed -e "N;N;N;N;N;s/\n/ /g;s/  */ /g" $test_dir/RESULTS | sort) <(sort $results)
