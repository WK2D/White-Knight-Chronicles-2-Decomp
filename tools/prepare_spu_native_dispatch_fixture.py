"""Extract the unchanged job dispatcher for a synthetic native-hook regression.

Only the dispatch function is emitted. The test includes the real workload
header and supplies the registry, fingerprint, logging and execution boundaries.
"""
import argparse
from pathlib import Path
import re


def extract_dispatch(source):
    matches = list(re.finditer(
        r"(?m)^int spu_workload_dispatch_job\([^;{}]*\)\s*\{", source))
    if len(matches) != 1:
        raise RuntimeError("Expected one production spu_workload_dispatch_job definition")
    match = matches[0]
    position, depth, state = match.end(), 1, "code"
    while position < len(source):
        char = source[position]
        following = source[position:position + 2]
        if state == "line":
            if char == "\n":
                state = "code"
        elif state == "comment":
            if following == "*/":
                state = "code"
                position += 1
        elif state in ('"', "'"):
            if char == "\\":
                position += 1
            elif char == state:
                state = "code"
        elif following == "//":
            state = "line"
            position += 1
        elif following == "/*":
            state = "comment"
            position += 1
        elif char in ('"', "'"):
            state = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():position + 1]
        position += 1
    raise RuntimeError("Unterminated production spu_workload_dispatch_job definition")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sdk", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source = (args.sdk / "runtime/spu/spu_workload.c").read_text(encoding="utf-8")
    dispatch = extract_dispatch(source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        "/* Generated from runtime/spu/spu_workload.c; function unchanged. */\n"
        + dispatch + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
