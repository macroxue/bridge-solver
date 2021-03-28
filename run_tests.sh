#!/bin/bash

test_dir=${1:-old_deals}
test_dir=${test_dir%%/*}  # remove trailing slashes

num_deals=0
start=$(date +"%s.%N")
for deal in $(ls $test_dir); do
  if [[ $deal = "RESULTS" ]]; then
    continue
  fi
  num_deals=$((num_deals+1))
  echo $deal
  ./solver -fi $test_dir/$deal
done > results.$test_dir
finish=$(date +"%s.%N")

echo Solved $num_deals deals in $(echo "scale=1;($finish-$start)/1" | bc) seconds.
echo Results are in \"results.$test_dir\".

diff $test_dir/RESULTS <(cut -c1-13 results.$test_dir)
