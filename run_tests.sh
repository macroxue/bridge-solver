#!/bin/bash

test_dir=${1:-fixed_deals}
test_dir=${test_dir%%/*}  # remove trailing slashes
results=results.$test_dir
echo Results are in $results
cat $test_dir/* > /dev/null  # bring files into cache

start=$(date +"%s.%N")
for deal in $(ls $test_dir -I RESULTS); do
  echo $deal
  ./solver -fi $test_dir/$deal -H0
done > $results
finish=$(date +"%s.%N")

num_deals=$(ls $test_dir -I RESULTS | wc -l)
echo Solved $num_deals deals in $(echo "scale=1;($finish-$start)/1" | bc) seconds

diff $test_dir/RESULTS <(cut -c1-13 $results)
