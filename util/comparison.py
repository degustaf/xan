#!/usr/bin/env python3

from math import sqrt
import sys

critical_values = [1e100, 31.821, 6.965, 4.541, 3.747, 3.365, 3.143,
                   2.998, 2.896, 2.821, 2.764]

def parse_data(file):
    data = None
    with open(file) as f:
        data = f.read()
    data = [x.split() for x in data.split('\n')]
    if data[-1] == []:
        return data[:-1]
    return data
    
old_data = parse_data('./before.txt')
new_data = parse_data('./after.txt')

def color_text(text, color):
  """Converts text to a string and wraps it in the ANSI escape sequence for
  color, if supported."""

  # No ANSI escapes on Windows.
  if sys.platform == 'win32':
    return str(text)

  return color + str(text) + '\033[0m'


def green(text):  return color_text(text, '\033[32m')
def yellow(text): return color_text(text, '\033[33m')
def red(text):    return color_text(text, '\033[31m')

def t_test(n1, n2):
    t_crit = critical_values[min(n1,n2)]
    def scale(x):
        if x > t_crit:
            return red(x)
        if x < -t_crit:
            return green(x)
        return yellow(x)

    def f(data):
        if data[0][0] != data[1][0]:
            print("benchmark mismatch")
            return
        mu1 = float(data[0][1])
        mu2 = float(data[1][1])
        s1 = float(data[0][2])
        s2 = float(data[1][2])

        t = (mu1 - mu2) / sqrt(s1 * s1 / n1 + s2 * s2 / n2)
        print("{0: <40}{1: <20}".format(data[0][0], scale(t)))

    return f

for _ in map(t_test(int(new_data[0][0]), int(old_data[0][0])),
             zip(new_data[1:], old_data[1:])):
    pass
