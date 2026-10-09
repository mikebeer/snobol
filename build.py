#!/usr/bin/env python3
"""Assemble src/*.crexx into snobol.crexx (symbolic constants + namespace list)."""
import re, os, sys
d = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'src')
order = ['a_head.crexx', 'b_parse.crexx', 'c_val.crexx', 'd_pat.crexx', 'e_eval.crexx', 'e_eval2.crexx', 'f_run.crexx']
order = [f for f in order if os.path.exists(os.path.join(d, f))]
src = ''.join(open(os.path.join(d, f)).read() for f in order)
consts = {}
for line in open(os.path.join(d, 'consts.txt')):
    line = line.split('#')[0].strip()
    if line:
        k, v = line.split()
        consts[k] = v
m = re.search(r'bfn = "([^"]*)"', src)
for i, w in enumerate(m.group(1).split()):
    consts['FN_' + w] = str(i + 1)
# namespace expose list: names assigned at 2-space indent inside init (not z_*)
body = src[src.index('init: procedure'):]
body = body[:body.index('\n  return\n')]
names = []
for ln in body.splitlines():
    mm = re.match(r'^  ([a-z][a-z0-9_]*) = ', ln)
    if mm and not mm.group(1).startswith('z_') and mm.group(1) not in names:
        names.append(mm.group(1))
lines = []
cur = 'namespace snobol expose'
for n in names:
    if len(cur) + len(n) > 74:
        lines.append(cur + ',')
        cur = '   ' + n
    else:
        cur += ' ' + n
lines.append(cur)
src = src.replace('@@EXPOSE@@', '\n'.join(lines))
def rep(mo):
    k = mo.group(0)
    if k in consts: return consts[k]
    raise SystemExit('unknown constant ' + k)
src = re.sub(r'\b(?:FN|N|PT|K|T|NM|KW)_[A-Za-z0-9]+\b', rep, src)
open(os.path.join(os.path.dirname(d), 'snobol.crexx'), 'w').write(src)
print(len(src.splitlines()), 'lines,', len(names), 'globals')
