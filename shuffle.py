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


def solve_once(code, seats):
    return run(['./solver', '-c', code, '-s', seats, '-d', '-m0'])


def print_header(seats, show_err, tricks_list):
    if seats == 'EW':
        print("      S        N      " if show_err else "      S    N  ", end='')
        for t in tricks_list:
            print(f"{t:2d}S {t:2d}N  ", end='')
    elif seats == 'NS':
        print("      W        E      " if show_err else "      W    E  ", end='')
        for t in tricks_list:
            print(f"{t:2d}W {t:2d}E  ", end='')
    else:  # NEW
        print("      S      " if show_err else "      S  ", end='')
        for t in tricks_list:
            print(f"{t:2d}S  ", end='')
    print()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('-c', dest='code')
    p.add_argument('-d', dest='random_deal', action='store_true')
    p.add_argument('-e', dest='show_err', action='store_true')
    p.add_argument('-p', dest='parallelism', type=int, default=1)
    p.add_argument('-r', dest='rounds', type=int, default=50)
    p.add_argument('-s', dest='single_seat', action='store_true')
    p.add_argument('-t', dest='min_tricks', type=int, default=7)
    p.add_argument('-v', dest='verbose', action='store_true')
    args = p.parse_args()

    code = args.code
    if args.random_deal:
        code = get_deal_code()
    if not code:
        code = get_random_code()

    subprocess.run(['./solver', '-c', code, '-m5'])

    seats_list = ['NEW'] if args.single_seat else ['EW', 'NS']
    tricks_list = list(range(args.min_tricks, 14))

    for seats in seats_list:
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

        print_header(seats, args.show_err, tricks_list)

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
            if seats == 'EW':
                left, right = 'S', 'N'
            elif seats == 'NS':
                left, right = 'W', 'E'
            else:
                left, right = 'S', None

            if args.show_err:
                out.append(f" {avg[left]:4.1f}±{err[left]:3.1f}")
                if right:
                    out.append(f" {avg[right]:4.1f}±{err[right]:3.1f}")
            else:
                out.append(f" {avg[left]:4.1f}")
                if right:
                    out.append(f" {avg[right]:4.1f}")

            for t in tricks_list:
                lr = histo[left][(row, t)] * 100 // args.rounds
                out.append(f"  {lr:3d}")
                if right:
                    rr = histo[right][(row, t)] * 100 // args.rounds
                    out.append(f" {rr:3d}")

            print(''.join(out))


if __name__ == '__main__':
    main()
