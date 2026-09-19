#!/bin/bash
#
# Like parallel_run.sh, but parallelizes at the (deal, strain) level
# instead of per-deal -- useful for directories with few but hard deals
# (e.g. deals/freak), where per-deal parallelism alone can't use more
# workers than there are deals.

if [[ $1 == '-r' ]]; then
  echo $3 $(./solver -if $2/$3 -t $4 -m0 | sed -e "s/[0-9]*\.[0-9]* [sM]//g")
  exit
fi

test_dir=${1:-deals/fixed}
test_dir=${test_dir%/}  # remove trailing slash
parallelism=${2:-2}
raw=results.$(basename $test_dir).$parallelism.strains.raw
results=results.$(basename $test_dir).$parallelism.strains
echo Results are in $results
cat $test_dir/* > /dev/null  # bring files into cache

deals=$(ls $test_dir | grep -v '^RESULTS$')
num_deals=$(echo "$deals" | wc -l)

TIMEFORMAT="Solved $num_deals deals in %1R seconds"
time (
  for deal in $deals; do
    for strain in N S H D C; do
      echo $deal $strain
    done
  done | xargs -n 2 -P $parallelism ./parallel_run_strain.sh -r $test_dir > $raw
)

# Regroup the scrambled (deal, strain) lines back into one N/S/H/D/C-ordered
# line per deal, matching RESULTS' layout for the diff below. One awk pass
# over the whole file, not one process per (deal, strain) pair.
awk '
{
  tricks = $3
  for (i = 4; i <= NF; i++) tricks = tricks" "$i
  data[$1,$2] = tricks
  seen[$1] = 1
}
END {
  n = split("N S H D C", strains, " ")
  for (deal in seen) {
    line = deal
    for (i = 1; i <= n; i++) line = line" "strains[i]" "data[deal,strains[i]]
    print line
  }
}' $raw | tr -s ' ' > $results
rm -f $raw

diff <(sed -e "N;N;N;N;N;s/\n/ /g;s/  */ /g" $test_dir/RESULTS | sort) <(sort $results)
