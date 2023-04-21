#!/bin/bash

code=
rounds=20
while getopts c:r: flag
do
  case $flag in
    c) code=$OPTARG;;
    r) rounds=$OPTARG;;
  esac
done

if [[ -z $code ]]; then
  output=($(./solver -ro -H1))
  code=${output[1]}
fi
./solver -c $code -H5

eraser="\b\b\b\b"

for seats in EW NS; do
  echo "Shuffling $seats ..."
  for trump in N S H D C; do
    south_sum=0
    north_sum=0
    west_sum=0
    east_sum=0
    for round in $(seq 1 $rounds); do
      if [[ -t 1 ]]; then
        printf "$round"
      fi
      result=($(./solver -c $code -s $seats -t $trump -d -H0))
      south_sum=$((south_sum+${result[1]}))
      north_sum=$((north_sum+${result[2]}))
      west_sum=$((west_sum+${result[3]}))
      east_sum=$((east_sum+${result[4]}))
      if [[ -t 1 ]]; then
        printf "$eraser"
      fi
    done

    south_avg=$(echo "scale=1;$south_sum/$rounds" | bc)
    north_avg=$(echo "scale=1;$north_sum/$rounds" | bc)
    west_avg=$(echo "scale=1;$west_sum/$rounds" | bc)
    east_avg=$(echo "scale=1;$east_sum/$rounds" | bc)
    if [[ $seats = EW ]]; then
      printf "$trump  %4.1f %4.1f (%4.1f %4.1f)\n" \
        $south_avg $north_avg $west_avg $east_avg
    else
      printf "$trump (%4.1f %4.1f) %4.1f %4.1f\n" \
        $south_avg $north_avg $west_avg $east_avg
    fi
  done
done
