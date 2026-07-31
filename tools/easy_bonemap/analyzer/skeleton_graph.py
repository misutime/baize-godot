"""Extract and normalize the factual skeleton graph from a GLTF document."""
from __future__ import annotations

from typing import Any
import numpy as np

from .gltf_reader import build_parent_map
from .transform_math import EPSILON, decompose_matrix, json_vector, unit_vector, local_matrix, world_matrices

FORMAT = "easy_bonemap.normalized_skeleton_graph.v1"


def _round_matrix(matrix: np.ndarray) -> list[float]:
    return [round(float(value), 8) for value in matrix.reshape(16, order="F")]


def _local_descriptor(node: Any) -> dict[str, Any]:
    matrix = getattr(node, "matrix", None)
    if isinstance(matrix, (list, tuple, np.ndarray)) and len(matrix) == 16:
        return {"matrix": [round(float(value), 8) for value in matrix]}
    translation = getattr(node, "translation", None) or [0.0, 0.0, 0.0]
    rotation = getattr(node, "rotation", None) or [0.0, 0.0, 0.0, 1.0]
    scale = getattr(node, "scale", None) or [1.0, 1.0, 1.0]
    return {
        "translation": [round(float(value), 8) for value in translation],
        "rotation": [round(float(value), 8) for value in rotation],
        "scale": [round(float(value), 8) for value in scale],
    }


def _path_scale(roots: list[int], children: dict[int, list[int]], edges: dict[tuple[int, int], float]) -> float:
    def longest(node: int) -> float:
        if not children[node]:
            return 0.0
        return max(edges[(node, child)] + longest(child) for child in children[node])

    return max((longest(root) for root in roots), default=0.0)


def _depths(roots: list[int], children: dict[int, list[int]]) -> dict[int, int]:
    result: dict[int, int] = {}
    stack = [(root, 0) for root in reversed(roots)]
    while stack:
        node, depth = stack.pop()
        result[node] = depth
        for child in reversed(children[node]):
            stack.append((child, depth + 1))
    return result


def extract_normalized_skeleton_graph(document: Any, source: str = "") -> dict[str, Any]:
    """Return a JSON-serializable, name-independent normalized skeleton graph."""
    report: dict[str, Any] = {
        "format": FORMAT,
        "source": source,
        "bone_count": 0,
        "bones": [],
        "roots": [],
        "degeneracies": [],
        "warnings": [],
    }
    skins = document.skins or []
    if not skins:
        report["degeneracies"].append("no_skins")
        report["warnings"].append("No skin joints were found")
        return report
    nodes = document.nodes or []
    try:
        parent_map = build_parent_map(document)
        matrices = world_matrices(document)
    except (ValueError, np.linalg.LinAlgError) as error:
        report["degeneracies"].append("invalid_node_transform_graph")
        report["warnings"].append(str(error))
        return report

    joint_indices: list[int] = []
    seen: set[int] = set()
    for skin_index, skin in enumerate(skins):
        joints = skin.joints or []
        if not joints:
            report["degeneracies"].append(f"empty_skin_joints:{skin_index}")
        for joint_index in joints:
            if not isinstance(joint_index, int) or not 0 <= joint_index < len(nodes):
                report["degeneracies"].append(f"joint_index_out_of_range:{skin_index}:{joint_index}")
                continue
            if joint_index in seen:
                report["warnings"].append(f"duplicate_joint_reference:{joint_index}")
                continue
            seen.add(joint_index)
            joint_indices.append(joint_index)
    if not joint_indices:
        report["degeneracies"].append("no_valid_joints")
        report["warnings"].append("No valid skeleton joints were found")
        return report

    joint_set = set(joint_indices)
    parent: dict[int, int] = {
        index: parent_map[index] if parent_map.get(index) in joint_set else -1 for index in joint_indices
    }
    children: dict[int, list[int]] = {
        index: [child for child in (nodes[index].children or []) if child in joint_set]
        for index in joint_indices
    }
    roots = [index for index in joint_indices if parent[index] == -1]
    report["roots"] = roots
    if len(roots) > 1:
        report["warnings"].append("multiple_skeleton_roots")

    positions = {index: matrices[index][:3, 3].copy() for index in joint_indices}
    edges: dict[tuple[int, int], float] = {}
    directions: dict[tuple[int, int], np.ndarray | None] = {}
    for parent_index, child_indices in children.items():
        for child_index in child_indices:
            edge = positions[child_index] - positions[parent_index]
            edges[(parent_index, child_index)] = float(np.linalg.norm(edge))
            if edges[(parent_index, child_index)] <= EPSILON:
                report["warnings"].append(
                    f"zero_length_joint_edge:{parent_index}:{child_index}"
                )
            directions[(parent_index, child_index)] = unit_vector(edge)
    scale = _path_scale(roots, children, edges)
    if scale <= EPSILON:
        report["degeneracies"].append("zero_skeleton_scale")
        report["warnings"].append("Skeleton has no measurable root-to-leaf length")
    origin = np.mean([positions[root] for root in roots], axis=0)
    depths = _depths(roots, children)
    bones: list[dict[str, Any]] = []
    for index in joint_indices:
        node = nodes[index]
        position, rotation, node_scale = decompose_matrix(matrices[index])
        normalized_position = None if scale <= EPSILON else (position - origin) / scale
        parent_edge = None
        if parent[index] != -1:
            edge_key = (parent[index], index)
            parent_edge = {
                "length": None if scale <= EPSILON else round(edges[edge_key] / scale, 8),
                "direction": json_vector(directions[edge_key]),
            }
        child_edges = []
        for child_index in children[index]:
            edge_key = (index, child_index)
            child_edges.append({
                "child": child_index,
                "length": None if scale <= EPSILON else round(edges[edge_key] / scale, 8),
                "direction": json_vector(directions[edge_key]),
            })
        bones.append({
            "index": index,
            "name": getattr(node, "name", None) or f"Node_{index}",
            "parent": parent[index],
            "local": _local_descriptor(node),
            "world_position": json_vector(position),
            "normalized_position": json_vector(normalized_position),
            "local_to_world": _round_matrix(matrices[index]),
            "depth": depths.get(index, 0),
            "world_rotation": json_vector(rotation),
            "world_scale": json_vector(node_scale),
            "parent_edge": parent_edge,
            "child_edges": child_edges,
        })
    report["bone_count"] = len(bones)
    report["bones"] = bones
    return report
