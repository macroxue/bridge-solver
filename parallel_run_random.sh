#!/bin/bash
#
# Solve <count> random deals, <parallelism> at a time.

count=${1:-100}
parallelism=${2:-2}
results=results.random.$count.$parallelism
echo Results are in $results

TIMEFORMAT="Solved $count deals in %1R seconds"
time (seq $count | xargs -n 1 -P $parallelism ./solver -r -m1 > $results)
