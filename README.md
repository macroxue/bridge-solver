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
                          ♠ KJT987 ♥ K5 ♦ 7 ♣ AQJ8
  ♠ 3 ♥ J9764 ♦ Q642 ♣ KT2                       ♠ Q64 ♥ QT8 ♦ KJ953 ♣ 94
                          ♠ A52 ♥ A32 ♦ AT8 ♣ 7653
N 13 13  0  0  0.01 s  10.2 M
S 13 13  0  0  0.02 s  10.2 M
H  7  7  6  5  0.21 s  12.9 M
D  6  6  6  6  0.34 s  13.2 M
C 13 13  0  0  0.34 s  13.2 M
```
Each line after the deal shows the strain to play, the number of tricks when
South/North/West/East declares respectively, the cumulative time and the peak
memory usage.

## Solve a deal in a file

```
./solver -f FILE
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
./solver -r -p
```
or
```
./solver -f FILE -p
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
The directory can be `fixed_deals` (the default), `old_deals`, `new_deals`, `hard_deals`,
`long_deals` or `1k_deals`. For parallel runs, the number of threads is 2 by default.
```
./run_tests.sh [DIRECTORY]
./parallel_run_tests.sh [DIRECTORY] [THREADS]
```

Benchmarks below run on [AMD Ryzen 7 5800H](https://www.amd.com/en/products/apu/amd-ryzen-7-5800h)
with 8 physical cores at 3.2GHz base clock and 4.4GHz boost clock.

### Single-core

The solver fully analyzed 1000 random deals (under `1k_deals`) in just 120 seconds,
averaging more than eight deals per second. Below is a more detailed breakdown.
The longest one (`deal.310`) took 1.24 seconds and consumed 48.2 MB of memory.

| Time  | <= 0.1s | <= 0.2s | <= 0.5s |  <= 1s  |  <= 2s  |
|-------|---------|---------|---------|---------|---------|
| Count |    607  |    857  |    976  |    999  |   1000  |

One of the most difficult deals is this symmetric one, with four void suits and
nobody holding consecutive ranks in any suit. It took the solver less than five seconds.
```
                          ♠ - ♥ Q853 ♦ AJ962 ♣ KT74
  ♠ KT74 ♥ - ♦ Q853 ♣ AJ962                       ♠ Q853 ♥ AJ962 ♦ KT74 ♣ -
                          ♠ AJ962 ♥ KT74 ♦ - ♣ Q853
N  5  5  5  5  2.47 s 154.6 M
S  4  4  8  7  2.89 s 154.9 M
H  8  7  4  4  3.53 s 154.9 M
D  4  4  7  8  4.18 s 154.9 M
C  7  8  4  4  4.65 s 154.9 M
```

An even more freakish deal with each player holding only two suits made the solver
work hard for 30 seconds!
```
                          ♠ KJ9753 ♥ - ♦ AQT8642 ♣ -
  ♠ AQT8642 ♥ KJ9753 ♦ - ♣ -                       ♠ - ♥ - ♦ KJ9753 ♣ AQT8642
                          ♠ - ♥ AQT8642 ♦ - ♣ KJ9753
N  7  7  7  7 21.06 s 158.4 M
S  6  6  7  7 22.77 s 158.4 M
H  7  7  6  6 24.79 s 158.4 M
D  7  7  6  6 28.80 s 158.4 M
C  6  6  7  7 30.33 s 158.4 M
```

### Multi-core

The table below shows the time for solving 1000 random deals in `1k_deals` with multiple cores.
The solver is single-threaded, so multiple instances of the solver are running in parallel.

| # Cores   |    1 |    2 |    4 |    8 |   16 |
|-----------|------|------|------|------|------|
| Time (s)  |120.0 | 65.8 | 35.5 | 21.9 | 17.9 |
| Speed-up  |  1.0 |  1.8 |  3.4 |  5.5 |  6.7 |

The scaling is decent up to 8 cores. 16 cores give small additional speed-up as the cores
are SMT threads rather than physical cores.

### Comparison

For single-threaded performance, the solver is 1.28x faster than
[DDS](https://github.com/dds-bridge/dds) and 1.75x faster than
[Bridge Calculator (bcalc)](http://bcalc.w8.pl/) on 5000 random deals.
The detailed run log is `comparison/results.5k_deals.txt`.

Since all the solvers are super fast on modern hardware, the difference is only noticeable
after 80 percentile as shown in the plot below.

![5k](https://github.com/macroxue/bridge-solver/blob/master/comparison/5k_deals.png)

A log-scale plot magnifies the difference. The gap between this solver and DDS is slightly
wider than the gap between DDS and bcalc.

![5k.log](https://github.com/macroxue/bridge-solver/blob/master/comparison/5k_deals.log.png)
