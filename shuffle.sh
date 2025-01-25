#!/bin/bash

code=
grand=0
show_err=0
rounds=50
seats_list="EW NS"
min_tricks=7
while getopts c:degr:st: flag
do
  case $flag in
    c) code=$OPTARG;;
    d)
      deal=($(./deal.sh))
      if [[ $? -ne 0 ]]; then exit; fi
      code=${deal[1]}
      ;;
    e) show_err=1;;
    g) grand=1;;
    r) rounds=$OPTARG;;
    s) seats_list="NEW";;
    t) min_tricks=$OPTARG;;
  esac
done

if [[ -z $code ]]; then
  output=($(./solver -ro -m1))
  code=${output[1]}
fi
./solver -c $code -m5

eraser="\b\b\b\b"
for seats in $seats_list; do
  if [[ $seats = EW ]]; then
    if [[ $show_err -eq 0 ]]; then
      printf "      S    N  "
    else
      printf "      S        N      "
    fi
    for tricks in $(seq $min_tricks 13); do
      printf "%2dS %2dN  " $tricks $tricks
    done
    echo
  elif [[ $seats = NS ]]; then
    if [[ $show_err -eq 0 ]]; then
      printf "      W    E  "
    else
      printf "      W        E      "
    fi
    for tricks in $(seq $min_tricks 13); do
      printf "%2dW %2dE  " $tricks $tricks
    done
    echo
  elif [[ $seats = NEW ]]; then
    if [[ $show_err -eq 0 ]]; then
      printf "      S  "
    else
      printf "      S      "
    fi
    for tricks in $(seq $min_tricks 13); do
      printf "%2dS  " $tricks
    done
    echo
  fi
  south_sum=(0 0 0 0 0)
  north_sum=(0 0 0 0 0)
  west_sum=(0 0 0 0 0)
  east_sum=(0 0 0 0 0)
  if [[ $show_err -eq 1 ]]; then
    south_sum2=(0 0 0 0 0)
    north_sum2=(0 0 0 0 0)
    west_sum2=(0 0 0 0 0)
    east_sum2=(0 0 0 0 0)
  fi
  declare -A south_histo
  declare -A north_histo
  declare -A west_histo
  declare -A east_histo
  for row in {0..4}; do
    for tricks in $(seq $min_tricks 13); do
      south_histo[$row,$tricks]=0
      north_histo[$row,$tricks]=0
      west_histo[$row,$tricks]=0
      east_histo[$row,$tricks]=0
    done
  done
  for round in $(seq 1 $rounds); do
    if [[ -t 1 ]]; then
      printf "$round"
    fi
    result=($(./solver -c $code -s $seats -d -m0))
    for row in {0..4}; do
      i=$((row*9))
      let south_sum[row]+=result[i+1]
      let north_sum[row]+=result[i+2]
      let west_sum[row]+=result[i+3]
      let east_sum[row]+=result[i+4]
      if [[ $show_err -eq 1 ]]; then
        let south_sum2[row]+=result[i+1]*result[i+1]
        let north_sum2[row]+=result[i+2]*result[i+2]
        let west_sum2[row]+=result[i+3]*result[i+3]
        let east_sum2[row]+=result[i+4]*result[i+4]
      fi
      for tricks in $(seq $min_tricks 13); do
        if [[ ${result[i+1]} -ge $tricks ]]; then let south_histo[$row,$tricks]++; fi
        if [[ ${result[i+2]} -ge $tricks ]]; then let north_histo[$row,$tricks]++; fi
        if [[ ${result[i+3]} -ge $tricks ]]; then let west_histo[$row,$tricks]++; fi
        if [[ ${result[i+4]} -ge $tricks ]]; then let east_histo[$row,$tricks]++; fi
      done
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
    fi
    if [[ $seats = EW ]]; then
      if [[ $show_err -eq 1 ]]; then
        printf "$trump  %4.1f±%3.1f %4.1f±%3.1f" $south_avg $south_err $north_avg $north_err
      else
        printf "$trump  %4.1f %4.1f" $south_avg $north_avg
      fi
      for tricks in $(seq $min_tricks 13); do
        south_rate=$(echo "${south_histo[$row,$tricks]}*100/$rounds" | bc)
        north_rate=$(echo "${north_histo[$row,$tricks]}*100/$rounds" | bc)
        printf "  %3d %3d" $south_rate $north_rate
      done
    elif [[ $seats = NS ]]; then
      if [[ $show_err -eq 1 ]]; then
        printf "$trump  %4.1f±%3.1f %4.1f±%3.1f" $west_avg $west_err $east_avg $east_err
      else
        printf "$trump  %4.1f %4.1f" $west_avg $east_avg
      fi
      for tricks in $(seq $min_tricks 13); do
        west_rate=$(echo "${west_histo[$row,$tricks]}*100/$rounds" | bc)
        east_rate=$(echo "${east_histo[$row,$tricks]}*100/$rounds" | bc)
        printf "  %3d %3d" $west_rate $east_rate
      done
    elif [[ $seats = NEW ]]; then
      if [[ $show_err -eq 1 ]]; then
        printf "$trump  %4.1f±%3.1f" $south_avg $south_err
      else
        printf "$trump  %4.1f" $south_avg
      fi
      for tricks in $(seq $min_tricks 13); do
        south_rate=$(echo "${south_histo[$row,$tricks]}*100/$rounds" | bc)
        printf "  %3d" $south_rate
      done
    fi
    printf "\n"
  done
done
