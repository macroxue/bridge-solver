#!/bin/bash

test_dir=${1:-deals/fixed}
test_dir=${test_dir%/}  # remove trailing slash
results=results.$(basename $test_dir)
if [[ -n $2 ]]; then
  results=$results.$2
fi
echo Results are in $results
cat $test_dir/* > /dev/null  # bring files into cache

list_deals() {
  # Portable replacement for GNU `ls -I RESULTS`.
  ls "$1" | grep -v '^RESULTS$'
}

start=$(date +"%s.%N")
for deal in $(list_deals "$test_dir"); do
  echo $deal
  ./solver -if $test_dir/$deal -m0
done > $results
finish=$(date +"%s.%N")

num_deals=$(list_deals "$test_dir" | wc -l)
echo Solved $num_deals deals in $(echo "scale=1;($finish-$start)/1" | bc) seconds

diff $test_dir/RESULTS <(cut -c1-13 $results)
