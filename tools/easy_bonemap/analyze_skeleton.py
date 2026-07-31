"""Analyze GLB skeleton structure and emit a compact report."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Sequence

from analyzer.candidate_generation import generate_body_candidates
from analyzer.pipeline import analyze_facts, analyze_glb_skeleton, read_glb_facts, write_json_report


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Analyze GLB skeleton structure and emit a compact report."
    )
    parser.add_argument("input", type=Path, help="Input .glb path")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="Compact skeleton analysis path; defaults to stdout",
    )
    parser.add_argument(
        "--debug-facts",
        type=Path,
        metavar="PATH",
        help="Optionally dump the full factual descriptor for debugging; not for normal AI input",
    )
    parser.add_argument(
        "--candidates",
        type=Path,
        metavar="PATH",
        help="Optionally write the body and limb candidate report as JSON",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.debug_facts is None and args.candidates is None:
        report = analyze_glb_skeleton(args.input)
    else:
        facts = read_glb_facts(args.input)
        if args.debug_facts is not None:
            write_json_report(facts, args.debug_facts)
            print(f"Wrote debug facts: {args.debug_facts}")
        report = analyze_facts(facts)
        if args.candidates is not None:
            candidates = generate_body_candidates(facts)
            write_json_report(candidates, args.candidates)
            print(f"Wrote body candidates: {args.candidates}")
    if args.output is None:
        print(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True))
        return 0
    write_json_report(report, args.output)
    print(f"Wrote compact skeleton analysis: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
