#!/bin/bash

code=
show_err=0
rounds=20
while getopts c:er: flag
do
  case $flag in
    c) code=$OPTARG;;
    e) show_err=1;;
    r) rounds=$OPTARG;;
  esac
done

if [[ -z $code ]]; then
  output=($(./solver -ro -m1))
  code=${output[1]}
fi
./solver -c $code -m5

eraser="\b\b\b\b"

for seats in EW NS; do
  echo "Shuffling $seats ..."
  for trump in N S H D C; do
    south_sum=0
    north_sum=0
    west_sum=0
    east_sum=0
    if [[ $show_err -eq 1 ]]; then
      south_sum2=0
      north_sum2=0
      west_sum2=0
      east_sum2=0
    fi
    for round in $(seq 1 $rounds); do
      if [[ -t 1 ]]; then
        printf "$round"
      fi
      result=($(./solver -c $code -s $seats -t $trump -d -m0))
      south_sum=$((south_sum+${result[1]}))
      north_sum=$((north_sum+${result[2]}))
      west_sum=$((west_sum+${result[3]}))
      east_sum=$((east_sum+${result[4]}))
      if [[ $show_err -eq 1 ]]; then
        south_sum2=$((south_sum2+${result[1]}*${result[1]}))
        north_sum2=$((north_sum2+${result[2]}*${result[2]}))
        west_sum2=$((west_sum2+${result[3]}*${result[3]}))
        east_sum2=$((east_sum2+${result[4]}*${result[4]}))
      fi
      if [[ -t 1 ]]; then
        printf "$eraser"
      fi
    done

    south_avg=$(echo "scale=1;$south_sum/$rounds" | bc)
    north_avg=$(echo "scale=1;$north_sum/$rounds" | bc)
    west_avg=$(echo "scale=1;$west_sum/$rounds" | bc)
    east_avg=$(echo "scale=1;$east_sum/$rounds" | bc)
    if [[ $show_err -eq 1 ]]; then
      south_err=$(echo "scale=1;sqrt(($south_sum2-$rounds*$south_avg*$south_avg)/($rounds-1))" | bc)
      north_err=$(echo "scale=1;sqrt(($north_sum2-$rounds*$north_avg*$north_avg)/($rounds-1))" | bc)
      west_err=$(echo "scale=1;sqrt(($west_sum2-$rounds*$west_avg*$west_avg)/($rounds-1))" | bc)
      east_err=$(echo "scale=1;sqrt(($east_sum2-$rounds*$east_avg*$east_avg)/($rounds-1))" | bc)
      if [[ $seats = EW ]]; then
        printf "$trump  %4.1f±%3.1f %4.1f±%3.1f (%4.1f±%3.1f %4.1f±%3.1f)\n" \
          $south_avg $south_err $north_avg $north_err $west_avg $west_err $east_avg $east_err
      else
        printf "$trump (%4.1f±%3.1f %4.1f±%3.1f) %4.1f±%3.1f %4.1f±%3.1f\n" \
          $south_avg $south_err $north_avg $north_err $west_avg $west_err $east_avg $east_err
      fi
    else
      if [[ $seats = EW ]]; then
        printf "$trump  %4.1f %4.1f (%4.1f %4.1f)\n" \
          $south_avg $north_avg $west_avg $east_avg
      else
        printf "$trump (%4.1f %4.1f) %4.1f %4.1f\n" \
          $south_avg $north_avg $west_avg $east_avg
      fi
    fi
  done
done
