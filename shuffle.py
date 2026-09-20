#!/usr/bin/env python3
"""Shuffle a deal N times and show trick distributions per strain/seat."""

import argparse
import math
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from time import monotonic

STRAINS = ['N', 'S', 'H', 'D', 'C']


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout


def run_for_code(cmd):
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(result.returncode)
    tokens = result.stdout.split()
    return tokens[1] if len(tokens) > 1 else ''


def get_deal_code():
    return run_for_code(['./deal.sh'])


def get_random_code():
    return run_for_code(['./solver', '-ro', '-m1'])


def get_file_code(path):
    return run_for_code(['./solver', '-f', path, '-o', '-m1'])


def solve_once(code, seats):
    return run(['./solver', '-c', code, '-s', seats, '-d', '-m0'])


def show_usage():
    print(f"{sys.argv[0]}  Estimate single-dummy trick-taking by shuffling one side's cards many times.")
    print(
        "\t-r           Shuffle a fully random deal.\n"
        "\t-b           Build a deal interactively (fixed hands via stdin). See deal.sh.\n"
        "\t-f <file>    Shuffle a deal in the input file. See files in *_deals/ for examples.\n"
        "\t-c <code>    Shuffle a deal defined by its unique code. See ./solver -m1.\n"
        "\n"
        "\t-n <rounds>  Number of shuffles to run. Default 50.\n"
        "\t-j <n>       Run shuffles in parallel using <n> threads. Default 1.\n"
        "\t-s <seats>   Shuffle these seats, a combination of {W, N, E, S}, instead of the\n"
        "\t             default EW and NS pairs.\n"
        "\t-m <tricks>  Lowest trick count shown in the histogram. Default 7.\n"
        "\t-e           Show standard error alongside averages.\n"
        "\t-v           Print timing information.")
    sys.exit(0)


def print_header(columns, show_err, tricks_list):
    avg_width = 9 if show_err else 5
    header = '  ' + ''.join(d.rjust(avg_width) for d in columns)
    for t in tricks_list:
        header += ''.join(f"{t:2d}{d}".rjust(5) for d in columns)
    print(header)


def main():
    if len(sys.argv) == 1:
        show_usage()

    p = argparse.ArgumentParser()
    p.add_argument('-b', dest='build_deal', action='store_true')
    p.add_argument('-c', dest='code')
    p.add_argument('-e', dest='show_err', action='store_true')
    p.add_argument('-f', dest='input_file')
    p.add_argument('-j', dest='parallelism', type=int, default=1)
    p.add_argument('-m', dest='min_tricks', type=int, default=7)
    p.add_argument('-n', dest='rounds', type=int, default=50)
    p.add_argument('-r', dest='randomize', action='store_true')
    p.add_argument('-s', dest='seats')
    p.add_argument('-v', dest='verbose', action='store_true')
    args = p.parse_args()

    if not (args.code or args.build_deal or args.input_file or args.randomize):
        show_usage()

    if args.seats and not set(args.seats.upper()) <= set('WNES'):
        sys.exit(f"Invalid seats: {args.seats} (use a combination of W, N, E, S)")

    code = args.code
    if args.build_deal:
        code = get_deal_code()
    if not code and args.input_file:
        code = get_file_code(args.input_file)
    if not code and args.randomize:
        code = get_random_code()

    subprocess.run(['./solver', '-c', code, '-m5'])

    seats_list = [args.seats] if args.seats else ['EW', 'NS']
    tricks_list = list(range(args.min_tricks, 14))

    for seats in seats_list:
        columns = [d for d in 'SNWE' if d not in seats.upper()]
        sums = {d: [0] * 5 for d in 'SNWE'}
        sums2 = {d: [0] * 5 for d in 'SNWE'}
        histo = {d: {(row, t): 0 for row in range(5) for t in tricks_list} for d in 'SNWE'}

        lines = []
        start = monotonic()
        if args.parallelism > 1:
            with ThreadPoolExecutor(max_workers=args.parallelism) as pool:
                futures = [pool.submit(solve_once, code, seats) for _ in range(args.rounds)]
                for i, future in enumerate(as_completed(futures)):
                    if sys.stdout.isatty():
                        sys.stdout.write(f"\r{i + 1}")
                        sys.stdout.flush()
                    lines.append(future.result())
            if sys.stdout.isatty():
                sys.stdout.write("\r" + " " * 8 + "\r")
        else:
            for i in range(args.rounds):
                if sys.stdout.isatty():
                    sys.stdout.write(f"\r{i + 1}")
                    sys.stdout.flush()
                lines.append(solve_once(code, seats))
            if sys.stdout.isatty():
                sys.stdout.write("\r" + " " * 8 + "\r")
        if args.verbose:
            print(f"Solved {args.rounds} shuffles in {monotonic() - start:.1f} seconds")

        print_header(columns, args.show_err, tricks_list)

        for line in lines:
            tok = line.split()
            for row in range(5):
                i = row * 9
                vals = dict(zip('SNWE', (int(tok[i + 1]), int(tok[i + 2]), int(tok[i + 3]), int(tok[i + 4]))))
                for d, v in vals.items():
                    sums[d][row] += v
                    if args.show_err:
                        sums2[d][row] += v * v
                    for t in tricks_list:
                        if v >= t:
                            histo[d][(row, t)] += 1

        for row in range(5):
            trump = STRAINS[row]
            avg = {d: sums[d][row] / args.rounds for d in 'SNWE'}
            err = {}
            if args.show_err:
                for d in 'SNWE':
                    if args.rounds > 1:
                        var = (sums2[d][row] - args.rounds * avg[d] ** 2) / (args.rounds - 1)
                        err[d] = math.sqrt(max(0.0, var))
                    else:
                        err[d] = 0.0

            out = [trump, ' ']
            for d in columns:
                if args.show_err:
                    out.append(f" {avg[d]:4.1f}±{err[d]:3.1f}")
                else:
                    out.append(f" {avg[d]:4.1f}")

            for t in tricks_list:
                for d in columns:
                    pct = histo[d][(row, t)] * 100 // args.rounds
                    out.append(f"  {pct:3d}")

            print(''.join(out))


if __name__ == '__main__':
    main()
