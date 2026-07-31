"""Extract a normalized factual Skeleton Graph from GLB/GLTF."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Sequence

from analyzer.gltf_reader import load_document
from analyzer.skeleton_graph import extract_normalized_skeleton_graph
from analyzer.validator import validate_asset


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Extract a normalized factual Skeleton Graph.")
    parser.add_argument("input", type=Path, help="Input .glb or .gltf path")
    parser.add_argument("-o", "--output", type=Path, help="Output JSON path; defaults to stdout")
    parser.add_argument("--skip-validator", action="store_true", help="Explicitly skip Khronos glTF Validator")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if not args.skip_validator:
        validator_report = validate_asset(args.input)
        issues = validator_report.get("issues", {})
        if int(issues.get("numErrors", 0)) > 0:
            raise ValueError(f"Input failed glTF validation with {issues['numErrors']} errors")
    document = load_document(args.input)
    report = extract_normalized_skeleton_graph(document, source=str(args.input))
    if args.skip_validator:
        report["warnings"].append("validator_skipped")
    if args.output is None:
        print(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True))
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"Wrote normalized skeleton graph: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
