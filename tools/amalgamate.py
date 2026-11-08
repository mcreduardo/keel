#!/usr/bin/env python3
"""Generates the single header keel.hpp from the parts in include/keel/.

    python3 tools/amalgamate.py          # writes keel.hpp
    python3 tools/amalgamate.py --check  # exits 1 if keel.hpp is not up to date

The parts are include/keel/impl/*.hpp, in the order include/keel/keel.hpp includes them.
Each part has the same shape: a comment, `#pragma once`, its #include lines, then its code
in one `namespace keel { ... }  // namespace keel` block. The generator keeps the code of
each block and drops the rest; it hoists the standard #includes of every part to the top
(each once, sorted) and drops the #includes of other parts. Where one part ends with
`}  // namespace detail` and the next begins with `namespace detail {`, the two lines are
dropped, so the code reads as one block. The result has the same layout as one hand-written
header: the comment below, #pragma once, the #includes, one namespace keel.
"""

import argparse
import difflib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENTRY = os.path.join(ROOT, "include", "keel", "keel.hpp")
OUTPUT = os.path.join(ROOT, "keel.hpp")

PREAMBLE = """\
// keel.hpp: application-level components with declared dependencies.
//
// Single header, C++20, no RTTI.
// Generated from include/keel/ by tools/amalgamate.py; do not edit.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once
"""

PART_INCLUDE = re.compile(r'^#include "(impl/[A-Za-z0-9_]+\.hpp)"$')
QUOTED_INCLUDE = re.compile(r'^#include "[^"]+"$')
STD_INCLUDE = re.compile(r"^#include <([^>]+)>$")
NAMESPACE_OPEN = "namespace keel {"
NAMESPACE_CLOSE = "}  // namespace keel"
DETAIL_OPEN = "namespace detail {"
DETAIL_CLOSE = "}  // namespace detail"


class Error(Exception):
    pass


def read_lines(path):
    with open(path, encoding="utf-8") as f:  # universal newlines: CRLF checkouts work too
        return f.read().split("\n")


def part_paths():
    """The parts, in the order the development header includes them."""
    paths = []
    for line in read_lines(ENTRY):
        match = PART_INCLUDE.match(line)
        if match:
            paths.append(os.path.join(ROOT, "include", "keel", match.group(1)))
    if not paths:
        raise Error(f"{ENTRY}: no #include \"impl/...\" lines")
    return paths


def strip_blank(lines):
    begin, end = 0, len(lines)
    while begin < end and not lines[begin].strip():
        begin += 1
    while end > begin and not lines[end - 1].strip():
        end -= 1
    return lines[begin:end]


def read_part(path):
    """The standard headers a part includes, and the code inside its namespace keel."""
    lines = read_lines(path)
    name = os.path.relpath(path, ROOT)
    try:
        open_at = lines.index(NAMESPACE_OPEN)
        close_at = len(lines) - 1 - lines[::-1].index(NAMESPACE_CLOSE)
    except ValueError:
        raise Error(f"{name}: expected one `{NAMESPACE_OPEN}` ... `{NAMESPACE_CLOSE}` block") from None
    headers = []
    for line in lines[:open_at]:
        match = STD_INCLUDE.match(line)
        if match:
            headers.append(match.group(1))
        elif not (line == "" or line.startswith("//") or line == "#pragma once" or QUOTED_INCLUDE.match(line)):
            raise Error(f"{name}: unexpected line before `{NAMESPACE_OPEN}`: {line}")
    if any(line.strip() for line in lines[close_at + 1:]):
        raise Error(f"{name}: unexpected lines after `{NAMESPACE_CLOSE}`")
    body = strip_blank(lines[open_at + 1:close_at])
    if not body:
        raise Error(f"{name}: empty")
    return headers, body


def amalgamate():
    headers = set()
    code = []
    for path in part_paths():
        part_headers, body = read_part(path)
        headers.update(part_headers)
        if code and code[-1] == DETAIL_CLOSE and body[0] == DETAIL_OPEN:
            code = strip_blank(code[:-1])
            body = strip_blank(body[1:])
        if code:
            code.append("")
        code.extend(body)
    out = [PREAMBLE, "\n"]
    out.extend(f"#include <{h}>\n" for h in sorted(headers))
    out.append(f"\n{NAMESPACE_OPEN}\n\n")
    out.extend(line + "\n" for line in code)
    out.append(f"\n{NAMESPACE_CLOSE}\n")
    return "".join(out)


def main():
    parser = argparse.ArgumentParser(description="Generate keel.hpp from include/keel/.")
    parser.add_argument("--check", action="store_true", help="exit 1 if keel.hpp differs from what would be generated")
    args = parser.parse_args()
    try:
        generated = amalgamate()
    except Error as e:
        print(f"amalgamate.py: {e}", file=sys.stderr)
        return 2
    if args.check:
        try:
            current = "\n".join(read_lines(OUTPUT))
        except FileNotFoundError:
            current = ""
        if current == generated:
            return 0
        diff = difflib.unified_diff(current.splitlines(), generated.splitlines(), "keel.hpp", "keel.hpp (generated)",
                                    lineterm="", n=1)
        print("keel.hpp is out of date: run python3 tools/amalgamate.py", file=sys.stderr)
        for i, line in enumerate(diff):
            if i == 40:
                print("...", file=sys.stderr)
                break
            print(line, file=sys.stderr)
        return 1
    with open(OUTPUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(generated)
    return 0


if __name__ == "__main__":
    sys.exit(main())
