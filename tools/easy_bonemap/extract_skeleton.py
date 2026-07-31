"""Extract a normalized factual Skeleton Graph from GLB/GLTF/FBX."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Sequence

from analyzer.fbx_converter import convert_fbx_to_glb
from analyzer.gltf_reader import load_document
from analyzer.skeleton_graph import extract_normalized_skeleton_graph
from analyzer.validator import validate_asset


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Extract a normalized factual Skeleton Graph.")
    parser.add_argument(
        "input",
        type=Path,
        help="Input .glb, .gltf, or .fbx path (FBX is converted to a temporary GLB via Blender)",
    )
    parser.add_argument("-o", "--output", type=Path, help="Output JSON path; defaults to stdout")
    parser.add_argument("--skip-validator", action="store_true", help="Explicitly skip Khronos glTF Validator")
    return parser


def _write_report(args: argparse.Namespace, report: dict) -> None:
    if args.output is None:
        print(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True))
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        print(f"Wrote normalized skeleton graph: {args.output}")


def _extract(args: argparse.Namespace, document_path: Path, *, is_fbx: bool) -> dict:
    """Validate, load, and extract; ``document_path`` is the asset to validate."""
    if not args.skip_validator:
        validator_report = validate_asset(document_path)
        issues = validator_report.get("issues", {})
        if int(issues.get("numErrors", 0)) > 0:
            raise ValueError(f"Input failed glTF validation with {issues['numErrors']} errors")
    document = load_document(document_path)
    report = extract_normalized_skeleton_graph(document, source=str(args.input))
    if is_fbx:
        report["warnings"].append("fbx_converted_via_blender")
    if args.skip_validator:
        report["warnings"].append("validator_skipped")
    return report


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    is_fbx = args.input.suffix.lower() == ".fbx"
    if is_fbx:
        # Convert to a temporary GLB first: the Khronos validator can only read
        # glTF bytes, so it validates the converted GLB. The original FBX path
        # remains the report source and the conversion is recorded as a warning.
        with convert_fbx_to_glb(args.input) as glb_path:
            report = _extract(args, glb_path, is_fbx=True)
            _write_report(args, report)
    else:
        report = _extract(args, args.input, is_fbx=False)
        _write_report(args, report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
