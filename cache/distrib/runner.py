#!/usr/bin/env python3
import os, sys, subprocess, struct, json

ALARM_TIMEOUT = 120
algo_input = json.loads("[291]")
algo = struct.pack("<I", len(algo_input))
for x in algo_input:
    algo += struct.pack("<I", x)

def check_algo_input(solutions, inputs):
    if len(solutions) != len(inputs):
        print("mismatching solutions and inputs length")
        return 0
    x = 0
    while x < len(solutions):
        sol = solutions[x]
        inp = inputs[x]

        cnt = 0
        orig = inp
        while inp != 1:
            if inp % 2 == 0:
                inp >>= 1
            else:
                inp = 3*inp + 1
            cnt += 1
        if cnt != sol:
            print ("for input %d sol %d was wrong (expected: %d)" % (orig, sol, cnt))
            return 0
        print("input %d sol %d matched expected" % (orig, sol))
        x += 1
    return 1

def run_challenge(inputs, dry_run=False):
    data = struct.pack("<I", len(inputs))
    for x in inputs:
        data += struct.pack("<I", x)

    challengeout, challengein = os.pipe()
    solutionout, solutionin = os.pipe()

    # write to input pipe and close it
    os.write(challengein, data)
    os.close(challengein)

    os.dup2(challengeout, 41)
    os.close(challengeout)
    os.dup2(solutionin, 42)
    os.close(solutionin)

    COMMAND = "./cache"

    if dry_run:
        # for a dry run, we just want the access count
        cp = subprocess.run(COMMAND, pass_fds=[41, 42], input=b"5\n", shell=True, capture_output=True, timeout=ALARM_TIMEOUT)
    else:
        cp = subprocess.run(COMMAND, pass_fds=[41, 42], shell=True, timeout=ALARM_TIMEOUT)
    os.close(41)
    os.close(42)
    if dry_run and cp.returncode != 0:
        print("dryrun failed, alert an admin")
        exit(1)

    solve = os.read(solutionout, 0x1000)
    sols = list(struct.unpack("<%dI" % (len(inputs) + 1), solve))
    access_cnt = sols.pop()
    os.close(solutionout)

    return (sols, access_cnt)

def calculate_score(correct, baseline, val):

    if correct:
        # 10 points for a correct result
        score = 10
    else:
        # no points for an incorrect solution
        return 0

    score += (baseline - val)

    # you can't get less than 0, or better than the baseline
    return min(max(score, 0), baseline)

try:
    # first, run it without any user input, so we can compare the raw number of accesses
    (_, baseline_access_cnt) = run_challenge(algo_input, dry_run=True)

    # ok, now run it with full stdin
    (sols, access_cnt) = run_challenge(algo_input)

    # check the solution
    print("checking...")
    correct = check_algo_input(sols, algo_input)

    score = calculate_score(correct, baseline_access_cnt, access_cnt)

    print("SCORE{%d}" % score)
    print("correct: %s (%d versus baseline %d)" % ("YES" if correct else "NO", access_cnt, baseline_access_cnt))
except Exception as e:
    print("got exception, bailing")
    print(e)
    exit(1)
