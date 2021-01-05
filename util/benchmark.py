#!/usr/bin/env python3

from math import sqrt
from os import listdir
from os.path import abspath, dirname, isdir, join, realpath, relpath
from subprocess import Popen, PIPE
import sys

def repeat(n, args):
    i = 0
    while i < n:
        out, err = Popen(args, stdin=PIPE, stdout=PIPE, stderr=PIPE).communicate()
        if(err):
            print("Error in " + path + ":\n")
            print(err.decode("utf-8"))
            return

        out = out.decode("utf-8").replace('\r\n', '\n')
        out_lines = out.split('\n')
        if out_lines[-1] == '':
            del out_lines[-1]
        yield float(out_lines[-1])
        i += 1


def runTest(path, n):
    args = ['./xan', path]
    total = 0
    sumSquares = 0

    print("{0: <40}".format(path), end='')
    sys.stdout.flush()
    for x in repeat(n, args):
        total += x
        sumSquares += x * x

    print("{0: <20}{1: <20}".format(total/n, sqrt((sumSquares - total * total / n) / (n-1))))

def walk(dir, callback):
    """
    Walks [dir], and executes [callback] on each file.
    """

    dir = abspath(dir)
    for file in listdir(dir):
        nfile = join(dir, file)
        if isdir(nfile):
            walk(nfile, callback)
        else:
            callback(nfile)

def run_script(n):
    def f(path):
        runTest(relpath(path).replace("\\", "/"), n)
    return f


if __name__ == '__main__':
    n = 5
    print('{0: <40}{1: <20}{2: <20}'.format(n, 'mean', 'std.dev.'))
    walk(join(dirname(dirname(realpath(__file__))), 'test/benchmark'), run_script(n))
