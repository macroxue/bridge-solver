#!/bin/bash

code=
show_err=0
rounds=20
seats_list="EW NS"
while getopts c:der:s flag
do
  case $flag in
    c) code=$OPTARG;;
    d) deal=($(./deal.sh));code=${deal[1]};;
    e) show_err=1;;
    r) rounds=$OPTARG;;
    s) seats_list="NEW";;
  esac
done

if [[ -z $code ]]; then
  output=($(./solver -ro -m1))
  code=${output[1]}
fi
./solver -c $code -m5

eraser="\b\b\b\b"

for seats in $seats_list; do
  echo "Shuffling $seats ..."
  south_sum=(0, 0, 0, 0, 0)
  north_sum=(0, 0, 0, 0, 0)
  west_sum=(0, 0, 0, 0, 0)
  east_sum=(0, 0, 0, 0, 0)
  if [[ $show_err -eq 1 ]]; then
    south_sum2=(0, 0, 0, 0, 0)
    north_sum2=(0, 0, 0, 0, 0)
    west_sum2=(0, 0, 0, 0, 0)
    east_sum2=(0, 0, 0, 0, 0)
  fi
  for round in $(seq 1 $rounds); do
    if [[ -t 1 ]]; then
      printf "$round"
    fi
    result=($(./solver -c $code -s $seats -d -m0))
    for row in {0..4}; do
      i=$((row*9))
      south_sum[row]=$((${south_sum[row]}+${result[i+1]}))
      north_sum[row]=$((${north_sum[row]}+${result[i+2]}))
      west_sum[row]=$((${west_sum[row]}+${result[i+3]}))
      east_sum[row]=$((${east_sum[row]}+${result[i+4]}))
      if [[ $show_err -eq 1 ]]; then
        south_sum2[row]=$((${south_sum2[row]}+${result[i+1]}*${result[i+1]}))
        north_sum2[row]=$((${north_sum2[row]}+${result[i+2]}*${result[i+2]}))
        west_sum2[row]=$((${west_sum2[row]}+${result[i+3]}*${result[i+3]}))
        east_sum2[row]=$((${east_sum2[row]}+${result[i+4]}*${result[i+4]}))
      fi
      if [[ -t 1 ]]; then
        printf "$eraser"
      fi
    done
  done

  strain=('N' 'S' 'H' 'D' 'C')
  for row in {0..4}; do
    trump=${strain[$row]}
    south_avg=$(echo "scale=1;${south_sum[row]}/$rounds" | bc)
    north_avg=$(echo "scale=1;${north_sum[row]}/$rounds" | bc)
    west_avg=$(echo "scale=1;${west_sum[row]}/$rounds" | bc)
    east_avg=$(echo "scale=1;${east_sum[row]}/$rounds" | bc)
    if [[ $show_err -eq 1 ]]; then
      south_err=$(echo "scale=1;sqrt((${south_sum2[row]}-$rounds*$south_avg*$south_avg)/($rounds-1))" | bc)
      north_err=$(echo "scale=1;sqrt((${north_sum2[row]}-$rounds*$north_avg*$north_avg)/($rounds-1))" | bc)
      west_err=$(echo "scale=1;sqrt((${west_sum2[row]}-$rounds*$west_avg*$west_avg)/($rounds-1))" | bc)
      east_err=$(echo "scale=1;sqrt((${east_sum2[row]}-$rounds*$east_avg*$east_avg)/($rounds-1))" | bc)
      if [[ $seats = EW ]]; then
        printf "$trump  %4.1f±%3.1f %4.1f±%3.1f (%4.1f±%3.1f %4.1f±%3.1f)\n" \
          $south_avg $south_err $north_avg $north_err $west_avg $west_err $east_avg $east_err
      elif [[ $seats = NS ]]; then
        printf "$trump (%4.1f±%3.1f %4.1f±%3.1f) %4.1f±%3.1f %4.1f±%3.1f\n" \
          $south_avg $south_err $north_avg $north_err $west_avg $west_err $east_avg $east_err
      elif [[ $seats = NEW ]]; then
        printf "$trump %4.1f±%3.1f (%4.1f±%3.1f %4.1f±%3.1f %4.1f±%3.1f)\n" \
          $south_avg $south_err $north_avg $north_err $west_avg $west_err $east_avg $east_err
      fi
    else
      if [[ $seats = EW ]]; then
        printf "$trump  %4.1f %4.1f (%4.1f %4.1f)\n" \
          $south_avg $north_avg $west_avg $east_avg
      elif [[ $seats = NS ]]; then
        printf "$trump (%4.1f %4.1f) %4.1f %4.1f\n" \
          $south_avg $north_avg $west_avg $east_avg
      elif [[ $seats = NEW ]]; then
        printf "$trump %4.1f (%4.1f %4.1f %4.1f)\n" \
          $south_avg $north_avg $west_avg $east_avg
      fi
    fi
  done
done
