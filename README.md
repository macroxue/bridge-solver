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
./solver -f [FILE]
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

`-i` flag can be used to ignore the strain and the leading seat that are
specified in the file.

## Solve an encoded deal

Each bridge deal is encoded by West, North and East's card holdings. When `-m`
flag value has its lowest bit set, the solver outputs this code. For example,
```
./solver -r -m3
# 3019E0620005,100A3AD222,2C419DB
                          ♠ J73 ♥ KJ9732 ♦ A62 ♣ 4
  ♠ AQ ♥ T65 ♦ JT9854 ♣ 98                       ♠ KT8642 ♥ A4 ♦ K ♣ Q652
                          ♠ 95 ♥ Q8 ♦ Q73 ♣ AKJT73
```

Then the code can be used to reproduce exactly the same deal like below.
```
./solver -c 3019E0620005,100A3AD222,2C419DB
```

## Solve a strain

```
./solver -r -t [STRAIN]
./solver -f [FILE] -t [STRAIN]
./solver -c [CODE] -t [STRAIN]
```
where [STRAIN] is one of {N, S, H, D, C}.

Solving a single deal with one thread per strain can be done with `xargs`:
```
echo N S H D C | xargs -n1 -P5 ./solver -if deals/freak/deal.0 -m0 -t
D 11 11  2  2  0.08 s   9.2 M
S  8  8  5  5  0.44 s  35.0 M
H  8  8  5  5  1.10 s  62.7 M
C  6  6  7  6  1.92 s  90.5 M
N  7  7  6  5  5.70 s 242.9 M
```

To get the strains sorted, pipe the previous command to
```
tr NSHDC ABCDE | sort | tr ABCDE NSHDC
```

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

## Single-dummy approximation

`./shuffle.sh` shuffles one side's cards while holding the other side's cards
fixed. In the example below, the first half is just the double-dummy result of
the deal; the second half are percentages of getting certain number of tricks,
according to double-dummy results of the shuffles.

```
# 801138827A000,858EA3208,1BCB0E2
                        N ♠ J52
                          ♥ K42
                          ♦ QJT2
                       12 ♣ AJ6
           W ♠ -                       E ♠ K876
             ♥ AQJT96                    ♥ 53
             ♦ K9874                     ♦ 6
          13 ♣ K2                      5 ♣ QT9854
                        S ♠ AQT943
                          ♥ 87
                          ♦ A53
                       10 ♣ 73
N  9  9  3  3  0.01 s   6.0 M
S 10  9  3  3  0.03 s   6.3 M
H  4  4  8  8  0.05 s   6.3 M
D  7  7  5  6  0.10 s  11.7 M
C  6  6  6  6  0.12 s  11.7 M
      S    N   7S  7N   8S  8N   9S  9N  10S 10N  11S 11N  12S 12N  13S 13N
N   7.3  7.7   66  72   54  54   36  36   22  26    6  10    0   0    0   0
S   9.4  9.4  100 100  100 100   76  76   48  50   14  16    4   4    0   0
H   4.6  4.7    4   6    2   2    0   0    0   0    0   0    0   0    0   0
D   7.9  8.1   84  88   56  66   30  30   14  16    4   6    4   4    0   0
C   4.6  4.8    4   6    2   2    0   0    0   0    0   0    0   0    0   0
      W    E   7W  7E   8W  8E   9W  9E  10W 10E  11W 11E  12W 12E  13W 13E
N   4.3  4.4    4   6    0   0    0   0    0   0    0   0    0   0    0   0
S   3.5  3.6    0   0    0   0    0   0    0   0    0   0    0   0    0   0
H   8.0  7.8   98  96   80  68   26  20    0   0    0   0    0   0    0   0
D   5.6  5.5   16  16    2   0    0   0    0   0    0   0    0   0    0   0
C   7.4  7.5   82  80   48  52   18  20    0   2    0   0    0   0    0   0
```

Specifically, `S   9.4  9.4` means either South or North averages 9.4 tricks
when declaring a spade contract. `48  50` on the same row shows South has 48%
chance of making 4♠ while North has a slightly higher chance at 50%.

## Performance

Run one of the following commands to measure performance and check correctness.
The directory can be `deals/fixed` (the default), `deals/old`, `deals/new`,
`deals/hard`, `deals/long`, `deals/1k` or `deals/freak`. For parallel runs,
the number of threads is 2 by default.
```
./run.sh [DIRECTORY]
./parallel_run.sh [DIRECTORY] [THREADS]
./parallel_run_strain.sh [DIRECTORY] [THREADS]
```

`parallel_run_strain.sh` parallelizes one thread per deal-strain pair instead of
one thread per deal, useful for directories with few but hard deals (e.g.
`deals/freak`) where per-deal parallelism alone can't use more threads than
there are deals.

To solve random deals instead of deals in a directory:
```
./parallel_run_random.sh [COUNT] [THREADS]
```

Benchmarks below run on [AMD Ryzen 7 5800H](https://www.amd.com/en/products/apu/amd-ryzen-7-5800h)
with 8 physical cores at 3.2GHz base clock and 4.4GHz boost clock. To get stable performance
numbers, all irrelevant applications are closed and `taskset` is used to bind the process
to a single core for single-core runs.

### Single-core

The solver fully analyzed 1000 random deals (under `deals/1k`) in just 93.8 seconds,
averaging more than ten deals per second. Below is a more detailed breakdown.
The longest one (`deal.310`) took 0.98 seconds and consumed 40.8 MB of memory.

| Time  | <= 0.1s | <= 0.2s | <= 0.5s |  <= 1s  |
|-------|---------|---------|---------|---------|
| Count |    719  |    912  |    992  |   1000  |

One of the most difficult deals is this symmetric one, with four void suits and
nobody holding consecutive ranks in any suit. It took the solver less than four seconds.
```
                          ♠ - ♥ Q853 ♦ AJ962 ♣ KT74
  ♠ KT74 ♥ - ♦ Q853 ♣ AJ962                       ♠ Q853 ♥ AJ962 ♦ KT74 ♣ -
                          ♠ AJ962 ♥ KT74 ♦ - ♣ Q853
N  5  5  5  5  1.88 s 132.5 M
S  4  4  8  7  2.21 s 133.3 M
H  8  7  4  4  2.65 s 133.9 M
D  4  4  7  8  3.02 s 133.9 M
C  7  8  4  4  3.39 s 133.9 M
```

An even more freakish deal with each player holding only two suits made the solver
work hard for more than 12 seconds!
```
                          ♠ KJ9753 ♥ - ♦ AQT8642 ♣ -
  ♠ AQT8642 ♥ KJ9753 ♦ - ♣ -                       ♠ - ♥ - ♦ KJ9753 ♣ AQT8642
                          ♠ - ♥ AQT8642 ♦ - ♣ KJ9753
N  7  7  7  7  7.48 s 109.9 M
S  6  6  7  7  8.43 s 110.2 M
H  7  7  6  6  9.74 s 110.7 M
D  7  7  6  6 11.33 s 111.0 M
C  6  6  7  7 12.54 s 111.3 M
```

A new champion has emerged when North and South switch hands in the symmetric
three-suited deal above. This simple change surprisingly increases the solving
time by more than 20x and the memory usage by nearly 13x, overwhelmingly just
for NT contracts.
```
                          ♠ AJ962 ♥ KT74 ♦ - ♣ Q853
  ♠ KT74 ♥ - ♦ Q853 ♣ AJ962                       ♠ Q853 ♥ AJ962 ♦ KT74 ♣ -
                          ♠ - ♥ Q853 ♦ AJ962 ♣ KT74
N  7  7  7  7 70.48 s 1688.0 M
S  4  4  7  7 70.87 s 1688.3 M
H  7  7  4  4 71.18 s 1688.3 M
D  4  4  7  7 71.47 s 1688.6 M
C  7  7  4  4 71.84 s 1688.6 M
```

### Multi-core

The table below shows the time for solving 1000 random deals in `deals/1k` with multiple cores.
The solver is single-threaded, so multiple instances of the solver are running in parallel.

| # Cores   |    1 |    2 |    4 |    8 |   16 |
|-----------|------|------|------|------|------|
| Time (s)  | 93.8 | 53.4 | 29.3 | 18.2 | 14.8 |
| Speed-up  |  1.0 |  1.8 |  3.2 |  5.2 |  6.3 |

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

**Sep 2026 update**: this solver has improved by 20% since the above
comparison, so it's 1.6x faster than DDS 2.9 and 2.1x faster than bcalc now.
Performance improvements seem to have stagnated with both DDS and bcalc.

## License

Licensed under either of [Apache License, Version 2.0](LICENSE-APACHE) or
[MIT license](LICENSE-MIT) at your option.
