# Bridge double dummy solver

This is a fairly simple and yet effective double dummy solver for the card
game of bridge. It's terminal based.

## Build the solver
Requirement: a Linux machine with G++ compiler installed.
```
make
```

## Solve a random deal
```
./solver -r
```
The output looks like below.
```
                         N ♠ KJT987 ♥ K5 ♦ 7 ♣ AQJ8
 W ♠ 3 ♥ J9764 ♦ Q642 ♣ KT2                      E ♠ Q64 ♥ QT8 ♦ KJ953 ♣ 94
                         S ♠ A52 ♥ A32 ♦ AT8 ♣ 7653
N 13 13  0  0  0.1 s   9.4 M
S 13 13  0  0  0.1 s   9.7 M
H  7  7  6  5  0.2 s  14.0 M
D  6  6  6  6  0.5 s  16.6 M
C 13 13  0  0  0.5 s  16.6 M
```
Each line after the deal shows the strain to play, the number of tricks when
South/North/West/East declares respectively, the cumulative time and the peak
memory usage.

## Solve a deal in a file

```
./solver -i FILE
```

The format of the deal in the file is like below.
```
               KQ3 - T832 AJ9765
72 AJ972 AQ7 KQ2               T96 K83 654 T843
               AJ854 QT654 KJ9 -
D
W
```
The first line is North. The second line has both West and East. The third line
is South. The forth line specifies the strain to play. The fifth line is the
leading seat. If the leading seat is not given, the deal is solved for all four
leading seats. If the strain to play is also not given, the deal is solved for
all five strains.

## Interactive play
```
./solver -r -I
```
or
```
./solver -i FILE -I
```

The solver automatically determines the contract. If nobody can make any
contract, the hand is skipped. For each turn, the solver evaluates each of the
player's card and shows the result of the contract if the card is played and
the rest is played by everyone optimally. A sign is shown next to each card
with the following meanings.
| Sign | Meaning |
|------|---------|
|  =   | The contract makes. |
|  +   | The contract gets an overtrick. |
|  -   | The contract is set by a trick. |
| (+N) | The contract gets N overtricks. |
| (-N) | The contract is set by N tricks. |

You can choose what card to play. For simplicity, only one of the equivalent
cards like QJT in the same suit can be chosen. You can also undo the plays
to explore all possibilities. Below is an example.
```
------ 3NT by NS: NS 0 EW 0 ------
                        N ♠ AK83
                          ♥ AK
                          ♦ A65432
                       21 ♣ K
           W ♠ 65                      E ♠ JT92
             ♥ QJT876                    ♥ 54
             ♦ KT9                       ♦ Q
          11 ♣ AJ                      3 ♣ 765432
                        S ♠ Q74
                          ♥ 932
                          ♦ J87
                        5 ♣ QT98
From ♠ 6+ ♥ Q=8= ♦ K(+2)T+ ♣ A+J+ West plays ♥ 8.
From ♥ A= North plays ♥ A.
From ♥ 5= East plays ♥ 5.
From ♥ 9=3= South plays ♥ 3.
------ 3NT by NS: NS 1 EW 0 ------
                        N ♠ AK83
                          ♥ K
                          ♦ A65432
                       17 ♣ K
           W ♠ 65                      E ♠ JT92
             ♥ QJT76                     ♥ 4
             ♦ KT9                       ♦ Q
          11 ♣ AJ                      3 ♣ 765432
                        S ♠ Q74
                          ♥ 92
                          ♦ J87
                        5 ♣ QT98
From ♠ A-8(-2)3(-2) ♥ K(-2) ♦ A-6(-2) ♣ K= North plays ♣ K?
```

## Performance

Run one of the following commands to measure performance and check correctness.
The directory can be `old_deals` (the default), `new_deals`, `hard_deals` or
`1k_deals`. For parallel runs, the number of threads is 2 by default.
```
./run_tests.sh [DIRECTORY]
./parallel_run_tests.sh [DIRECTORY] [THREADS]
```

On ThinkPad X1 Carbon running with Intel(R) Core(TM) i7-6600U CPU @ 2.60GHz,
the solver was able to fully analyze 1000 random deals in just 436 seconds,
averaging less than half a second per deal. Below is a more detailed breakdown.
The longest one took 6.1 seconds and consumed 84.3 MB of memory.
| Time    | Count   |
|---------|---------|
| <= .5 s | 776     |
| <= 1 s  | 924     |
| <= 2 s  | 977     |
| <= 5 s  | 998     |
| <= 10 s | 1000    |

The most difficult deal known to the author is this symmetric one, with four
void suits and nobody holding consecutive ranks in any suit. It took the solver
just over half a minute.
```
                  - Q853 AJ962 KT74
KT74 - Q853 AJ962                   Q853 AJ962 KT74 -
                  AJ962 KT74 - Q853
N  5  5  5  5 24.9 s 456.7 M
S  4  4  8  7 27.3 s 456.7 M
H  8  7  4  4 30.7 s 456.7 M
D  4  4  7  8 33.6 s 456.7 M
C  7  8  4  4 36.7 s 456.7 M
```
