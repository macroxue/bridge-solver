#!/bin/bash

code=
show_err=0
rounds=20
seats_list="EW NS"
while getopts c:der:s flag
do
  case $flag in
    c) code=$OPTARG;;
    d)
      deal=($(./deal.sh))
      if [[ $? -ne 0 ]]; then exit; fi
      code=${deal[1]}
      ;;
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
  south_games=(0 0 0 0 0)
  north_games=(0 0 0 0 0)
  west_games=(0 0 0 0 0)
  east_games=(0 0 0 0 0)
  south_slams=(0 0 0 0 0)
  north_slams=(0 0 0 0 0)
  west_slams=(0 0 0 0 0)
  east_slams=(0 0 0 0 0)
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
      game_tricks=$((9+(row+1)/2))
      if [[ ${result[i+1]} -ge $game_tricks ]]; then let south_games[$row]++; fi
      if [[ ${result[i+2]} -ge $game_tricks ]]; then let north_games[$row]++; fi
      if [[ ${result[i+3]} -ge $game_tricks ]]; then let west_games[$row]++; fi
      if [[ ${result[i+4]} -ge $game_tricks ]]; then let east_games[$row]++; fi
      if [[ ${result[i+1]} -ge 12 ]]; then let south_slams[$row]++; fi
      if [[ ${result[i+2]} -ge 12 ]]; then let north_slams[$row]++; fi
      if [[ ${result[i+3]} -ge 12 ]]; then let west_slams[$row]++; fi
      if [[ ${result[i+4]} -ge 12 ]]; then let east_slams[$row]++; fi
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
    south_game=$(echo "${south_games[row]}*100/$rounds" | bc)
    north_game=$(echo "${north_games[row]}*100/$rounds" | bc)
    west_game=$(echo "${west_games[row]}*100/$rounds" | bc)
    east_game=$(echo "${east_games[row]}*100/$rounds" | bc)
    south_slam=$(echo "${south_slams[row]}*100/$rounds" | bc)
    north_slam=$(echo "${north_slams[row]}*100/$rounds" | bc)
    west_slam=$(echo "${west_slams[row]}*100/$rounds" | bc)
    east_slam=$(echo "${east_slams[row]}*100/$rounds" | bc)
    if [[ $seats = EW ]]; then
      printf "$trump  %4.1f %4.1f (%4.1f %4.1f)" $south_avg $north_avg $west_avg $east_avg
      if [[ $show_err -eq 1 ]]; then
        printf "  ±%3.1f ±%3.1f (±%3.1f ±%3.1f)" $south_err $north_err $west_err $east_err
      fi
      printf "  %3d %3d (%3d %3d)" $south_game $north_game $west_game $east_game
      printf "  %3d %3d (%3d %3d)" $south_slam $north_slam $west_slam $east_slam
    elif [[ $seats = NS ]]; then
      printf "$trump (%4.1f %4.1f) %4.1f %4.1f" $south_avg $north_avg $west_avg $east_avg
      if [[ $show_err -eq 1 ]]; then
        printf "  (±%3.1f ±%3.1f) ±%3.1f ±%3.1f" $south_err $north_err $west_err $east_err
      fi
      printf "  (%3d %3d) %3d %3d" $south_game $north_game $west_game $east_game
      printf "  (%3d %3d) %3d %3d" $south_slam $north_slam $west_slam $east_slam
    elif [[ $seats = NEW ]]; then
      printf "$trump %4.1f (%4.1f %4.1f %4.1f)" $south_avg $north_avg $west_avg $east_avg
      if [[ $show_err -eq 1 ]]; then
        printf "  ±%3.1f (±%3.1f ±%3.1f ±%3.1f)" $south_err $north_err $west_err $east_err
      fi
      printf "  %3d (%3d %3d %3d)" $south_game $north_game $west_game $east_game
      printf "  %3d (%3d %3d %3d)" $south_slam $north_slam $west_slam $east_slam
    fi
    printf "\n"
  done
done
