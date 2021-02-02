#!/usr/bin/env python3

from math import sqrt
from os import listdir
from os.path import abspath, dirname, isdir, join, realpath, relpath
from subprocess import Popen, PIPE
import sys
from timeit import default_timer as timer

def repeat(n, path, args):
    i = 0
    
    while i < n:
        start = timer()
        _, err = Popen(args, stdin=PIPE, stdout=PIPE, stderr=PIPE).communicate()
        end = timer()
        if(err):
            print("Error in " + path + ":\n")
            print("'" + err.decode("utf-8") + "'")
            return

        yield float(end - start)
        i += 1


def runTest(interpreter, path, n):
    args = [interpreter, path]
    total = 0
    sumSquares = 0

    print("{0: <40}".format(path), end='')
    sys.stdout.flush()
    for x in repeat(n, path, args):
        total += x
        sumSquares += x * x

    print("{0: <20} {1: <20}".format(total/n, sqrt((sumSquares - total * total / n) / (n-1))))

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

def run_script(interpreter, n):
    def f(path):
        runTest(interpreter, relpath(path).replace("\\", "/"), n)
    return f

def main(interpreter="./xan", n=2):
    print('{0: <40}{1: <20}{2: <20}'.format(n, 'mean', 'std.dev.'))
    walk(join(dirname(dirname(realpath(__file__))), 'test/benchmark'), run_script(interpreter, n))

if __name__ == '__main__':
    main(*sys.argv[1:])
