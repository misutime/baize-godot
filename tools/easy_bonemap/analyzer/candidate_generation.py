"""Deterministic body and limb candidate generation for EasyBoneMap.

This module proposes candidates from factual GLB data. It does not choose a
final mapping, infer missing bones, or write Godot resources.
"""

from __future__ import annotations

import math
import re
from typing import Any


FORMAT = "easy_bonemap.body_candidates.v1"
PROFILE = "SkeletonProfileHumanoid"
SCOPE = "body_and_limbs"

BODY_SLOTS = (
    "Root",
    "Hips",
    "Spine",
    "Chest",
    "Neck",
    "Head",
    "LeftUpperArm",
    "LeftLowerArm",
    "LeftHand",
    "RightUpperArm",
    "RightLowerArm",
    "RightHand",
    "LeftUpperLeg",
    "LeftLowerLeg",
    "LeftFoot",
    "RightUpperLeg",
    "RightLowerLeg",
    "RightFoot",
)

_SLOT_PARTS = {
    "Root": "root",
    "Hips": "hips",
    "Spine": "spine",
    "Chest": "chest",
    "Neck": "neck",
    "Head": "head",
    "LeftUpperArm": "upper_arm",
    "LeftLowerArm": "lower_arm",
    "LeftHand": "hand",
    "RightUpperArm": "upper_arm",
    "RightLowerArm": "lower_arm",
    "RightHand": "hand",
    "LeftUpperLeg": "upper_leg",
    "LeftLowerLeg": "lower_leg",
    "LeftFoot": "foot",
    "RightUpperLeg": "upper_leg",
    "RightLowerLeg": "lower_leg",
    "RightFoot": "foot",
}

_PART_ALIASES = {
    "root": ("root", "armature", "skeleton"),
    "hips": ("hips", "hip", "pelvis"),
    "spine": ("spine", "back", "torso"),
    "chest": ("chest", "upperchest", "upper_torso", "torso"),
    "neck": ("neck",),
    "head": ("head", "skull"),
    "upper_arm": ("upperarm", "upper_arm", "arm", "shoulder"),
    "lower_arm": ("lowerarm", "lower_arm", "forearm", "fore_arm", "elbow"),
    "hand": ("hand", "wrist"),
    "upper_leg": ("upperleg", "upper_leg", "thigh", "hip"),
    "lower_leg": ("lowerleg", "lower_leg", "calf", "shin", "knee"),
    "foot": ("foot", "ankle"),
}

_VECTOR_FEATURES = ("topology", "position", "direction", "length", "symmetry", "skinning")


def _clamp(value: float, low: float = 0.0, high: float = 1.0) -> float:
    return max(low, min(high, value))


def _sub(left: tuple[float, float, float], right: tuple[float, float, float]) -> tuple[float, float, float]:
    return tuple(left[index] - right[index] for index in range(3))  # type: ignore[return-value]


def _add(left: tuple[float, float, float], right: tuple[float, float, float]) -> tuple[float, float, float]:
    return tuple(left[index] + right[index] for index in range(3))  # type: ignore[return-value]


def _length(value: tuple[float, float, float]) -> float:
    return math.sqrt(sum(component * component for component in value))


def _distance(left: tuple[float, float, float], right: tuple[float, float, float]) -> float:
    return _length(_sub(left, right))


def _translation(transform: Any) -> tuple[float, float, float]:
    if not isinstance(transform, dict):
        return (0.0, 0.0, 0.0)
    value = transform.get("translation")
    if isinstance(value, (list, tuple)) and len(value) >= 3:
        return tuple(float(value[index]) for index in range(3))  # type: ignore[return-value]
    matrix = transform.get("matrix")
    if isinstance(matrix, (list, tuple)):
        flat: list[float] = []
        for item in matrix:
            if isinstance(item, (list, tuple)):
                flat.extend(float(part) for part in item)
            else:
                flat.append(float(item))
        if len(flat) >= 16:
            return (flat[12], flat[13], flat[14])
    return (0.0, 0.0, 0.0)


def _normalise_name(name: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")


def _has_side(name: str, side: str) -> bool:
    normalized = _normalise_name(name)
    tokens = set(normalized.split("_"))
    if side == "left":
        return "left" in tokens or "l" in tokens or normalized.startswith("l_")
    if side == "right":
        return "right" in tokens or "r" in tokens or normalized.startswith("r_")
    return False


def _name_features(name: str, slot: str) -> tuple[float, float, list[str]]:
    normalized = _normalise_name(name)
    part = _SLOT_PARTS[slot]
    aliases = _PART_ALIASES[part]
    side = "left" if slot.startswith("Left") else "right" if slot.startswith("Right") else None
    exact = _normalise_name(slot) == normalized
    tokens = set(normalized.split("_"))
    part_match = any(alias in normalized for alias in aliases)
    if part == "root" and tokens.intersection({"hip", "hips", "pelvis"}):
        part_match = False
    side_match = side is None or _has_side(name, side)
    opposite_match = side is not None and _has_side(name, "right" if side == "left" else "left")
    evidence: list[str] = []
    if exact:
        evidence.append("exact profile-name match")
    elif part_match:
        evidence.append(f"name contains {part} alias")
    if side is not None and side_match:
        evidence.append(f"name indicates {side} side")
    if opposite_match:
        evidence.append("name indicates opposite side")
    if exact:
        return 1.0, 1.0, evidence
    if part_match and side_match:
        return 0.9, 0.85, evidence
    if part_match:
        return 0.65, 0.65, evidence
    return 0.0, 0.0, evidence


def _build_bones(facts: dict[str, Any]) -> list[dict[str, Any]]:
    raw_nodes = [node for node in facts.get("nodes", []) if isinstance(node, dict)]
    if not raw_nodes:
        return []
    by_index = {int(node["index"]): node for node in raw_nodes if "index" in node}
    joint_indices: set[int] = set()
    for skin in facts.get("skins", []):
        for joint in skin.get("joints", []):
            if isinstance(joint, dict) and isinstance(joint.get("node_index"), int):
                joint_indices.add(joint["node_index"])
    if not joint_indices:
        joint_indices = set(by_index)

    children: dict[int, list[int]] = {
        index: [child for child in node.get("children", []) if child in joint_indices]
        for index, node in by_index.items()
        if index in joint_indices
    }
    parent: dict[int, int | None] = {
        index: (node.get("parent_index") if node.get("parent_index") in joint_indices else None)
        for index, node in by_index.items()
        if index in joint_indices
    }
    depths: dict[int, int] = {}

    def depth(index: int, active: set[int] | None = None) -> int:
        if index in depths:
            return depths[index]
        active = active or set()
        if index in active or parent[index] is None:
            depths[index] = 0
        else:
            depths[index] = depth(parent[index], active | {index}) + 1  # type: ignore[arg-type]
        return depths[index]

    local_positions = {index: _translation(node.get("transform")) for index, node in by_index.items()}
    positions: dict[int, tuple[float, float, float]] = {}

    def world_position(index: int, active: set[int] | None = None) -> tuple[float, float, float]:
        if index in positions:
            return positions[index]
        active = active or set()
        if index in active or parent[index] is None:
            positions[index] = local_positions[index]
        else:
            positions[index] = _add(world_position(parent[index], active | {index}), local_positions[index])  # type: ignore[arg-type]
        return positions[index]

    for index in sorted(joint_indices):
        depth(index)
        world_position(index)

    skin_stats: dict[int, dict[str, float]] = {}
    for binding in facts.get("skinning", []):
        for joint in binding.get("joints", []):
            index = joint.get("node_index")
            if not isinstance(index, int):
                continue
            stats = skin_stats.setdefault(index, {"influenced": 0.0, "dominant": 0.0, "weight": 0.0})
            stats["influenced"] += float(joint.get("influenced_vertex_count", 0) or 0)
            stats["dominant"] += float(joint.get("dominant_vertex_count", 0) or 0)
            stats["weight"] += float(joint.get("weight_sum", 0.0) or 0.0)

    result: list[dict[str, Any]] = []
    for index in sorted(joint_indices):
        node = by_index[index]
        parent_index = parent[index]
        position = positions[index]
        parent_position = positions[parent_index] if parent_index is not None else None
        if parent_position is not None:
            direction = _sub(position, parent_position)
            segment_length = _length(direction)
        else:
            child_positions = [positions[child] for child in children[index] if child in positions]
            direction = _sub(child_positions[0], position) if child_positions else (0.0, 1.0, 0.0)
            segment_length = _length(direction)
        result.append(
            {
                "index": index,
                "name": str(node.get("name", f"Node_{index}")),
                "parent_index": parent_index,
                "children": children[index],
                "depth": depths[index],
                "position": position,
                "direction": direction,
                "length": segment_length,
                "skin": skin_stats.get(index, {"influenced": 0.0, "dominant": 0.0, "weight": 0.0}),
            }
        )
    return result


def _side(slot: str) -> str | None:
    if slot.startswith("Left"):
        return "left"
    if slot.startswith("Right"):
        return "right"
    return None


def _position_score(bone: dict[str, Any], slot: str, minimum_y: float, height: float, lateral_extent: float) -> float:
    x, y, _ = bone["position"]
    y_norm = (y - minimum_y) / height if height > 1e-8 else 0.5
    lateral = abs(x) / max(lateral_extent, 1e-8)
    side = _side(slot)
    if side == "left":
        side_score = 0.7 + 0.3 * _clamp(lateral / 0.25) if x < 0.0 else 0.0
    elif side == "right":
        side_score = 0.7 + 0.3 * _clamp(lateral / 0.25) if x > 0.0 else 0.0
    else:
        side_score = _clamp(1.0 - lateral)

    if slot == "Root":
        vertical = 1.0 - y_norm
    elif slot == "Hips":
        vertical = 1.0 - abs(y_norm - 0.55) / 0.65
    elif slot == "Spine":
        vertical = 1.0 - abs(y_norm - 0.65) / 0.55
    elif slot == "Chest":
        vertical = 1.0 - abs(y_norm - 0.78) / 0.45
    elif slot == "Neck":
        vertical = 1.0 - abs(y_norm - 0.88) / 0.35
    elif slot == "Head":
        vertical = y_norm
    elif "Arm" in slot or slot.endswith("Hand"):
        vertical = 1.0 - abs(y_norm - 0.75) / 0.40
    elif "Leg" in slot or slot.endswith("Foot"):
        vertical = 1.0 - abs(y_norm - 0.30) / 0.40
    else:
        vertical = 0.5
    return _clamp(0.60 * _clamp(vertical) + 0.40 * side_score)


def _topology_score(bone: dict[str, Any], slot: str, bones_by_index: dict[int, dict[str, Any]]) -> float:
    parent = bone["parent_index"]
    child_count = len(bone["children"])
    depth = bone["depth"]
    if slot == "Root":
        return 1.0 if parent is None else 0.0
    if slot == "Hips":
        return _clamp((0.55 if parent is not None else 0.0) + (0.45 if child_count >= 2 else 0.15))
    if slot in {"Spine", "Chest", "Neck"}:
        return _clamp((0.35 if parent is not None else 0.0) + (0.35 if child_count >= 1 else 0.0) + (0.1 * min(depth, 3)))
    if slot == "Head":
        return _clamp((0.55 if parent is not None else 0.0) + (0.3 if child_count <= 2 else 0.0))
    if slot.endswith("Hand") or slot.endswith("Foot"):
        return _clamp((0.55 if parent is not None else 0.0) + (0.35 if child_count <= 2 else 0.05))
    return _clamp((0.45 if parent is not None else 0.0) + (0.35 if child_count >= 1 else 0.1))


def _direction_score(bone: dict[str, Any], slot: str) -> float:
    direction = bone["direction"]
    magnitude = _length(direction)
    if magnitude <= 1e-8:
        return 0.25
    vertical = abs(direction[1]) / magnitude
    lateral = abs(direction[0]) / magnitude
    if slot in {"Spine", "Chest", "Neck", "Head", "Hips"}:
        return _clamp(0.45 + 0.55 * vertical)
    if "Arm" in slot or slot.endswith("Hand"):
        return _clamp(0.45 + 0.55 * lateral)
    if "Leg" in slot:
        return _clamp(0.45 + 0.55 * vertical)
    if slot.endswith("Foot"):
        return _clamp(0.4 + 0.6 * max(lateral, 1.0 - vertical))
    return 0.5


def _length_score(bone: dict[str, Any], slot: str, median_length: float) -> float:
    if median_length <= 1e-8 or bone["length"] <= 1e-8:
        return 0.35
    ratio = bone["length"] / median_length
    if slot in {"Hips", "Root", "Head", "Neck"}:
        expected = (0.25, 2.5)
    elif slot.endswith("Hand") or slot.endswith("Foot"):
        expected = (0.1, 1.5)
    else:
        expected = (0.2, 3.0)
    if expected[0] <= ratio <= expected[1]:
        return 1.0
    distance = expected[0] - ratio if ratio < expected[0] else ratio - expected[1]
    return _clamp(1.0 - distance / max(expected[1], 1.0))


def _symmetry_score(bone: dict[str, Any], bones: list[dict[str, Any]], slot: str, height: float) -> float:
    side = _side(slot)
    if side is None:
        return 1.0 if abs(bone["position"][0]) <= height * 0.2 else 0.55
    x, y, z = bone["position"]
    best = None
    for other in bones:
        if other["index"] == bone["index"]:
            continue
        ox, oy, oz = other["position"]
        if x * ox >= 0.0:
            continue
        distance = abs(y - oy) + abs(z - oz) + abs(abs(x) - abs(ox))
        if best is None or distance < best:
            best = distance
    if best is None:
        return 0.35
    return _clamp(1.0 - best / max(height, 1e-8))


def _skinning_score(bone: dict[str, Any], bones: list[dict[str, Any]]) -> float:
    maximum = max((other["skin"]["influenced"] for other in bones), default=0.0)
    if maximum <= 0.0:
        return 0.35
    return _clamp(bone["skin"]["influenced"] / maximum)


def _candidate_for(
    bone: dict[str, Any],
    slot: str,
    bones: list[dict[str, Any]],
    minimum_y: float,
    height: float,
    lateral_extent: float,
    median_length: float,
    ) -> dict[str, Any]:
    name_score, name_match, name_evidence = _name_features(bone["name"], slot)
    features = {
        "topology": round(_topology_score(bone, slot, {item["index"]: item for item in bones}), 6),
        "position": round(_position_score(bone, slot, minimum_y, height, lateral_extent), 6),
        "direction": round(_direction_score(bone, slot), 6),
        "length": round(_length_score(bone, slot, median_length), 6),
        "symmetry": round(_symmetry_score(bone, bones, slot, height), 6),
        "skinning": round(_skinning_score(bone, bones), 6),
        "name": round(name_score, 6),
    }
    score = sum(features[name] * weight for name, weight in (("topology", 0.25), ("position", 0.20), ("direction", 0.15), ("length", 0.10), ("symmetry", 0.15), ("skinning", 0.15)))
    if name_match >= 1.0:
        score = max(score, 0.96)
    elif name_match >= 0.85:
        score = max(score, 0.72)
    evidence = list(name_evidence)
    if features["topology"] >= 0.7:
        evidence.append("compatible parent/child topology")
    if features["position"] >= 0.7:
        evidence.append("compatible body position and side")
    if features["direction"] >= 0.7:
        evidence.append("compatible bone direction")
    if features["symmetry"] >= 0.7:
        evidence.append("has a mirrored spatial counterpart")
    if features["skinning"] >= 0.65:
        evidence.append("has substantial skinning influence")
    return {
        "skeleton_bone": bone["name"],
        "score": round(_clamp(score), 6),
        "features": features,
        "evidence": evidence or ["structural evidence only"],
        "_name_score": name_score,
        "_index": bone["index"],
    }


def generate_body_candidates(facts: dict[str, Any]) -> dict[str, Any]:
    """Generate explainable Root/Hips/body/limb candidates from GLB facts."""
    bones = _build_bones(facts)
    report: dict[str, Any] = {
        "format": FORMAT,
        "source": facts.get("source", ""),
        "asset": facts.get("asset", {}),
        "profile": PROFILE,
        "scope": SCOPE,
        "slots": [],
        "warnings": [],
    }
    if not bones:
        report["warnings"].append("No skeleton joints were found")
        report["slots"] = [{"profile_bone": slot, "status": "unknown", "candidates": []} for slot in BODY_SLOTS]
        return report
    if len(bones) == 1:
        report["slots"] = [
            {
                "profile_bone": slot,
                "status": "candidate" if slot == "Root" else "unknown",
                "candidates": [] if slot != "Root" else [_candidate_for(bones[0], "Root", bones, 0.0, 1.0, 1.0, 0.0)],
            }
            for slot in BODY_SLOTS
        ]
        return report

    positions = [bone["position"] for bone in bones]
    minimum_y = min(position[1] for position in positions)
    maximum_y = max(position[1] for position in positions)
    height = max(maximum_y - minimum_y, 1e-8)
    lengths = [bone["length"] for bone in bones if bone["length"] > 1e-8]
    median_length = sorted(lengths)[len(lengths) // 2] if lengths else 0.0
    lateral_extent = max((abs(position[0]) for position in positions), default=0.0)
    lateral_extent = max(lateral_extent, height)

    for slot in BODY_SLOTS:
        candidates = [_candidate_for(bone, slot, bones, minimum_y, height, lateral_extent, median_length) for bone in bones]
        candidates.sort(key=lambda item: (-item["score"], -item["_name_score"], item["_index"], item["skeleton_bone"]))
        named_candidates = [candidate for candidate in candidates if candidate["_name_score"] >= 0.65]
        if named_candidates:
            candidates = named_candidates
        candidates = [candidate for candidate in candidates if candidate["score"] >= 0.5][:5]
        for candidate in candidates:
            candidate.pop("_name_score", None)
            candidate.pop("_index", None)
        if not candidates:
            status = "unknown"
        elif len(candidates) > 1 and candidates[0]["score"] - candidates[1]["score"] < 0.08:
            status = "ambiguous"
        else:
            status = "candidate"
        report["slots"].append({"profile_bone": slot, "status": status, "candidates": candidates})

    if not facts.get("skins"):
        report["warnings"].append("No skin binding was available; skinning scores are neutral")
    return report
