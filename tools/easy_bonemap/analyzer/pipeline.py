"""Pipeline functions for EasyBoneMap."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from analyzer.glb_reader import read_glb
from analyzer.skeleton_graph import analyze_skeleton_graph
from analyzer.candidate_generation import generate_body_candidates


def read_glb_facts(input_path: str | Path) -> dict[str, Any]:
    """Read one GLB and return only facts present in the asset."""
    return read_glb(input_path)

def generate_glb_candidates(input_path: str | Path) -> dict[str, Any]:
    """Read one GLB and generate body and limb candidates."""
    return generate_body_candidates(read_glb_facts(input_path))


def write_json_report(report: dict[str, Any], output_path: str | Path) -> None:
    """Persist one already-computed report as JSON."""
    destination = Path(output_path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(
        json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def analyze_facts(facts: dict[str, Any]) -> dict[str, Any]:
    """Analyze already-loaded facts without re-reading the source asset."""
    totals: dict[str, dict[str, int]] = {}
    for binding in facts.get("skinning", []):
        for joint in binding.get("joints", []):
            name = joint["name"]
            total = totals.setdefault(
                name,
                {"influenced_vertex_count": 0, "dominant_vertex_count": 0},
            )
            total["influenced_vertex_count"] += joint["influenced_vertex_count"]
            total["dominant_vertex_count"] += joint["dominant_vertex_count"]

    top_influences = [
        {"name": name, **counts}
        for name, counts in sorted(
            totals.items(),
            key=lambda item: (-item[1]["influenced_vertex_count"], item[0]),
        )[:20]
    ]
    return {
        "format": facts["format"],
        "source": facts["source"],
        "asset": facts["asset"],
        "mesh_count": len(facts.get("meshes", [])),
        "skin_count": len(facts.get("skins", [])),
        "skeleton": analyze_skeleton_graph(facts),
        "skinning": {
            "binding_count": len(facts.get("skinning", [])),
            "top_influences": top_influences,
        },
        "next_stage": "hand_analysis",
        "facts_retained_in_memory": True,
    }


def analyze_glb_skeleton(input_path: str | Path) -> dict[str, Any]:
    """Read GLB facts and return a compact structural skeleton report."""
    return analyze_facts(read_glb_facts(input_path))

