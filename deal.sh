#!/bin/bash
#
# To get a random deal:
#   ./deal.sh < /dev/null
#
# To get a random deal with a fixed South hand:
#   echo "♠ AKJ74 ♥ -    ♦ A95  ♣ KQ832" | ./deal.sh
#   echo "S:AKJ74 H:-    D:A95  C:KQ832" | ./deal.sh
#   echo "AKJ74 - A95 KQ832" | ./deal.sh
#   ./deal.sh <<END
#   ♠ AKJ74
#   ♥ -
#   ♦ A95
#   ♣ KQ832
#   END
#
# To get a random deal with fixed North-South hands:
#   printf "♠ AKJ74 ♥ -    ♦ A95  ♣ KQ832\n♠ Q92 ♥ AT73 ♦ T3 ♣ AJ96" | ./deal.sh
#   echo "S AKJ74 H -    D A95  C KQ832
#         ♠ Q92 ♥ AT73 ♦ T3 ♣ AJ96" | ./deal.sh
#   ./deal.sh <<END
#   AKJ74
#   -
#   A95
#   KQ832
#   Q92
#   AT73
#   T3
#   AJ96
#   END

nocard_line="^[ 	SHDC:♣♦♥♠]+$"
card_line="^[AaKkQqJjTt1098765432 	SHDC:♣♦♥♠-]+$"

./solver -H3 -i <((
  echo "         - - - -          "
  echo "- - - -            - - - -"
  egrep -v "$nocard_line" | \
    egrep "$card_line" | \
    sed "N;N;N;s/\n/ /g" | \
    sed "N;s/\n/\n- - - -            - - - -\n/") | tail -3)
