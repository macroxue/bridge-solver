# Bridge double dummy solver

This is a fairly simple and yet effective double dummy solver for the card
game of bridge. It's written in C++ and terminal based.

Try the [web demo](https://macroxue.github.io/bridge-solver/web/), which
runs the same solver compiled to WebAssembly.

## Build the solver
Requirement: a Linux machine with G++ compiler installed.
```
make
```

To build the web demo, [Emscripten](https://emscripten.org) is required.
```
make web
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
N 13 13  0  0  0.00 s   5.3 M
S 13 13  0  0  0.01 s   5.3 M
H  7  7  6  5  0.07 s   7.9 M
D  6  6  6  6  0.09 s   7.9 M
C 13 13  0  0  0.09 s   7.9 M
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
The directory can be `deals/fixed` (the default), `deals/old`, `deals/new`, `deals/hard`,
`deals/long` or `deals/1k`. For parallel runs, the number of threads is 2 by default.
```
./run_tests.sh [DIRECTORY]
./parallel_run_tests.sh [DIRECTORY] [THREADS]
```

Benchmarks below run on [AMD Ryzen 7 5800H](https://www.amd.com/en/products/apu/amd-ryzen-7-5800h)
with 8 physical cores at 3.2GHz base clock and 4.4GHz boost clock.

### Single-core

The solver fully analyzed 1000 random deals (under `deals/1k`) in just 112.8 seconds,
averaging nearly nine deals per second. Below is a more detailed breakdown.
The longest one (`deal.310`) took 1.18 seconds and consumed 45.0 MB of memory.

| Time  | <= 0.1s | <= 0.2s | <= 0.5s |  <= 1s  |  <= 2s  |
|-------|---------|---------|---------|---------|---------|
| Count |    637  |    872  |    982  |    999  |   1000  |

One of the most difficult deals is this symmetric one, with four void suits and
nobody holding consecutive ranks in any suit. It took the solver less than four seconds.
```
                          ♠ - ♥ Q853 ♦ AJ962 ♣ KT74
  ♠ KT74 ♥ - ♦ Q853 ♣ AJ962                       ♠ Q853 ♥ AJ962 ♦ KT74 ♣ -
                          ♠ AJ962 ♥ KT74 ♦ - ♣ Q853
N  5  5  5  5  1.89 s 128.6 M
S  4  4  8  7  2.27 s 129.4 M
H  8  7  4  4  2.76 s 129.9 M
D  4  4  7  8  3.30 s 130.7 M
C  7  8  4  4  3.68 s 130.7 M
```

An even more freakish deal with each player holding only two suits made the solver
work hard for almost 16 seconds!
```
                          ♠ KJ9753 ♥ - ♦ AQT8642 ♣ -
  ♠ AQT8642 ♥ KJ9753 ♦ - ♣ -                       ♠ - ♥ - ♦ KJ9753 ♣ AQT8642
                          ♠ - ♥ AQT8642 ♦ - ♣ KJ9753
N  7  7  7  7  9.16 s  64.5 M
S  6  6  7  7 10.61 s  64.8 M
H  7  7  6  6 12.21 s  65.0 M
D  7  7  6  6 13.94 s  65.0 M
C  6  6  7  7 15.52 s  65.0 M
```

A new champion has emerged when North and South switch hands in the symmetric
three-suited deal above. This simple change surprisingly increases the solving
time by more than 19x and the memory usage by nearly 13x, overwhelmingly just
for NT contracts.
```
                          ♠ AJ962 ♥ KT74 ♦ - ♣ Q853
  ♠ KT74 ♥ - ♦ Q853 ♣ AJ962                       ♠ Q853 ♥ AJ962 ♦ KT74 ♣ -
                          ♠ - ♥ Q853 ♦ AJ962 ♣ KT74
N  7  7  7  7 69.00 s 1680.2 M
S  4  4  7  7 69.42 s 1680.2 M
H  7  7  4  4 69.77 s 1680.4 M
D  4  4  7  7 70.12 s 1680.4 M
C  7  7  4  4 70.50 s 1680.4 M
```

### Multi-core

The table below shows the time for solving 1000 random deals in `deals/1k` with multiple cores.
The solver is single-threaded, so multiple instances of the solver are running in parallel.

| # Cores   |    1 |    2 |    4 |    8 |   16 |
|-----------|------|------|------|------|------|
| Time (s)  |112.8 | 63.6 | 35.0 | 21.6 | 17.5 |
| Speed-up  |  1.0 |  1.8 |  3.2 |  5.2 |  6.4 |

The scaling is decent up to 8 cores. 16 cores give small additional speed-up as the cores
are SMT threads rather than physical cores.

### Comparison

For single-threaded performance, the solver is 1.36x faster than
[DDS 2.9](https://github.com/dds-bridge/dds) and 1.75x faster than
[Bridge Calculator (bcalc)](http://bcalc.w8.pl/) on 5000 random deals.
The detailed run log is `comparison/results.5k_deals.txt`.

Since all the solvers are super fast on modern hardware, the difference is only noticeable
after 80 percentile as shown in the plot below.

![5k](https://github.com/macroxue/bridge-solver/blob/master/comparison/5k_deals.png)

A log-scale plot magnifies the difference. The gap between this solver and DDS is slightly
wider than the gap between DDS and bcalc.

![5k.log](https://github.com/macroxue/bridge-solver/blob/master/comparison/5k_deals.log.png)

**Aug 2026 update**: this solver has improved by 15% since the above
comparison, so it's 1.5x faster than DDS 2.9 and 2x faster than bcalc now.
Performance improvements seem to have stagnated with both DDS and bcalc.

## License

Licensed under either of [Apache License, Version 2.0](LICENSE-APACHE) or
[MIT license](LICENSE-MIT) at your option.
