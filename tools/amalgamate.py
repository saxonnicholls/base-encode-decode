#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2024 Saxon Nicholls
"""
Amalgamate a C++ entry file and its local (quote) #includes into a single,
self-contained translation unit - ideal for pasting into Compiler Explorer
(godbolt.org) to inspect how tight the generated code is.

Every  #include "..."  is resolved (against the including file's directory, then
each -I directory) and inlined exactly once. System includes  #include <...>  are
left EXACTLY WHERE THEY ARE - never hoisted or de-duplicated - so headers that
include e.g. <immintrin.h> only inside an `#if defined(__SSSE3__)` guard stay
correct on every target. Their own include guards make any repeats harmless.

Usage:
    tools/amalgamate.py ENTRY [-I DIR ...] [-o OUT] [--no-markers]

Examples:
    # One .cpp with main() for godbolt (the dependency-free demo):
    tools/amalgamate.py BaseEncodeDecode/main.cpp -I BaseEncodeDecode -o build/godbolt.cpp

    # A single distributable header: point ENTRY at a small umbrella header that
    # #includes the parts you want, and emit one header:
    tools/amalgamate.py umbrella.hpp -I BaseEncodeDecode -o base_encode_decode_single.hpp
"""

import argparse
import os
import re
import sys

QUOTE_INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"')
ANGLE_INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*<[^>]+>')


def resolve(target, current_dir, include_dirs):
    """Find a quote-included file: relative to the including file, then each -I dir."""
    for base in [current_dir, *include_dirs]:
        candidate = os.path.normpath(os.path.join(base, target))
        if os.path.isfile(candidate):
            return candidate
    return None


def amalgamate(entry, include_dirs, markers=True):
    entry = os.path.normpath(entry)
    inlined = set()          # resolved local files already emitted
    out = []

    def emit_file(path):
        real = os.path.normpath(path)
        if real in inlined:
            return           # already inlined once - skip
        inlined.add(real)
        rel = os.path.relpath(real)
        current_dir = os.path.dirname(real)
        if markers:
            out.append(f'// ===================== begin {rel} =====================')
        with open(real, encoding='utf-8') as handle:
            for line in handle:
                text = line.rstrip('\n')
                match = QUOTE_INCLUDE.match(text)
                if match:
                    resolved = resolve(match.group(1), current_dir, include_dirs)
                    if resolved is not None:
                        emit_file(resolved)          # inline it here, in place
                        continue
                    sys.stderr.write(
                        f'warning: unresolved #include "{match.group(1)}" in {rel} '
                        f'(left as-is)\n')
                    out.append(text)
                    continue
                # System includes (and everything else) pass through unchanged.
                out.append(text)
        if markers:
            out.append(f'// ====================== end {rel} ======================')

    emit_file(entry)

    header = [
        '// Amalgamated by tools/amalgamate.py into a single translation unit.',
        f'// Entry: {os.path.relpath(entry)}',
        '// Local includes inlined once; system <...> includes left in place.',
        '',
    ]
    return '\n'.join(header + out) + '\n'


def main():
    parser = argparse.ArgumentParser(
        description='Inline local #includes into one self-contained file.')
    parser.add_argument('entry', help='the C++ file to amalgamate (a .cpp or .hpp)')
    parser.add_argument('-I', dest='include_dirs', action='append', default=[],
                        metavar='DIR', help='include directory (repeatable)')
    parser.add_argument('-o', dest='out', default=None,
                        help='output file (default: stdout)')
    parser.add_argument('--no-markers', dest='markers', action='store_false',
                        help='omit the "begin/end <file>" comment markers')
    args = parser.parse_args()

    if not os.path.isfile(args.entry):
        sys.exit(f'error: entry file not found: {args.entry}')

    result = amalgamate(args.entry, args.include_dirs, markers=args.markers)

    if args.out:
        os.makedirs(os.path.dirname(args.out) or '.', exist_ok=True)
        with open(args.out, 'w', encoding='utf-8') as handle:
            handle.write(result)
        sys.stderr.write(f'wrote {args.out} '
                         f'({len(result.splitlines())} lines, {len(result)} bytes)\n')
    else:
        sys.stdout.write(result)


if __name__ == '__main__':
    main()
