#!/usr/bin/env python3

"""
Script to execute tests, including benchmarks, and collect data on frequency of
strings of opcodes used.

Follows the analysis of O'Donoghue and Power:
    http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.107.4212&rep=rep1&type=pdf
"""

from os import listdir
from os.path import abspath, dirname, isdir, join, realpath, relpath
import re
from subprocess import Popen, PIPE

def opcodes(file_name):
    OP_REGEX = re.compile(r'^\s+(OP_[A-Z_]+),\s*// Registers: ([ABCD,]+)')
    with open(file_name, 'r') as file:
        return [(match.group(1), match.group(2).split(',')) 
                for match in [OP_REGEX.search(line) for line in file]
                if match]

def walk(dir, callback, results):
    """
    Walks [dir], and executes [callback] on each file.
    """

    dir = abspath(dir)
    for file in listdir(dir):
        nfile = join(dir, file)
        if isdir(nfile):
            reults = walk(nfile, callback, results)
        else:
            results = callback(nfile, results)

    return results


REPO_DIR = dirname(dirname(realpath(__file__)))
ops = opcodes(join(REPO_DIR, 'src/chunk.h'))
print(ops)

def run_script(file, results):
    # Normalize it to use "/"
    file = relpath(file).replace("\\", "/")
    args = [join(REPO_DIR, "xan"), "-b", file]
    proc = Popen(args, stdin=PIPE, stdout=PIPE, stderr=PIPE)

    out, _ = proc.communicate()
    if proc.returncode == 0:
        print(out.decode().splitlines())

    return results

results = walk(join(REPO_DIR, 'test'), run_script, {})
