"""Fact-only GLB extraction for EasyBoneMap.

This module deliberately does not classify bones or infer humanoid semantics.
It only decodes facts present in the GLB: nodes, transforms, skins, meshes,
and skinning attributes.
"""

from __future__ import annotations

import json
import struct
from pathlib import Path
from typing import Any


class GlbReadError(ValueError):
    """Raised when a GLB cannot be decoded without guessing."""


_COMPONENTS: dict[int, tuple[str, int, bool]] = {
    5120: ("b", 1, False),
    5121: ("B", 1, False),
    5122: ("h", 2, False),
    5123: ("H", 2, False),
    5125: ("I", 4, False),
    5126: ("f", 4, True),
}
_TYPE_COMPONENT_COUNTS = {
    "SCALAR": 1,
    "VEC2": 2,
    "VEC3": 3,
    "VEC4": 4,
    "MAT2": 4,
    "MAT3": 9,
    "MAT4": 16,
}


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise GlbReadError(message)


def _read_glb(path: Path) -> tuple[dict[str, Any], bytes]:
    if path.suffix.lower() != ".glb":
        raise GlbReadError(f"Expected a .glb file, got: {path}")
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise GlbReadError(f"Cannot read GLB: {path}: {exc}") from exc

    _require(len(data) >= 12, "GLB is shorter than its 12-byte header")
    magic, version, declared_length = struct.unpack_from("<4sII", data, 0)
    _require(magic == b"glTF", "Invalid GLB magic")
    _require(version == 2, f"Unsupported glTF binary version: {version}")
    _require(declared_length == len(data), "GLB header length does not match file length")

    json_bytes: bytes | None = None
    binary = b""
    offset = 12
    while offset < len(data):
        _require(offset + 8 <= len(data), "Truncated GLB chunk header")
        chunk_length, chunk_type = struct.unpack_from("<I4s", data, offset)
        offset += 8
        end = offset + chunk_length
        _require(end <= len(data), "Truncated GLB chunk payload")
        payload = data[offset:end]
        offset = end
        if chunk_type == b"JSON":
            _require(json_bytes is None, "GLB contains more than one JSON chunk")
            json_bytes = payload.rstrip(b" \t\r\n\x00")
        elif chunk_type == b"BIN\x00":
            _require(binary == b"", "GLB contains more than one BIN chunk")
            binary = payload

    _require(json_bytes is not None, "GLB does not contain a JSON chunk")
    try:
        document = json.loads(json_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise GlbReadError(f"Invalid GLB JSON chunk: {exc}") from exc
    _require(isinstance(document, dict), "GLB JSON root must be an object")
    return document, binary


def _node_transform(node: dict[str, Any]) -> dict[str, Any]:
    transform: dict[str, Any] = {}
    for key in ("matrix", "translation", "rotation", "scale"):
        if key in node:
            transform[key] = node[key]
    return transform


def _accessor_values(
    document: dict[str, Any],
    binary: bytes,
    accessor_index: int,
) -> list[Any]:
    accessors = document.get("accessors", [])
    buffer_views = document.get("bufferViews", [])
    buffers = document.get("buffers", [])
    _require(0 <= accessor_index < len(accessors), f"Invalid accessor index: {accessor_index}")
    accessor = accessors[accessor_index]
    _require(isinstance(accessor, dict), f"Accessor {accessor_index} is not an object")
    _require("bufferView" in accessor, f"Accessor {accessor_index} has no bufferView")
    _require("sparse" not in accessor, f"Sparse accessors are not supported: {accessor_index}")
    view_index = accessor["bufferView"]
    _require(0 <= view_index < len(buffer_views), f"Invalid bufferView index: {view_index}")
    view = buffer_views[view_index]
    _require(view.get("buffer", 0) == 0, "Only the first GLB buffer is supported")
    component_type = accessor.get("componentType")
    _require(component_type in _COMPONENTS, f"Unsupported accessor component type: {component_type}")
    fmt, component_size, _ = _COMPONENTS[component_type]
    value_type = accessor.get("type")
    component_count = _TYPE_COMPONENT_COUNTS.get(value_type)
    _require(component_count is not None, f"Unsupported accessor type: {value_type}")

    element_size = component_size * component_count
    stride = view.get("byteStride", element_size)
    _require(isinstance(stride, int) and stride >= element_size, "Invalid bufferView byteStride")
    base = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    count = accessor.get("count")
    _require(isinstance(count, int) and count >= 0, f"Invalid accessor count: {accessor_index}")
    end = base + (count - 1) * stride + element_size if count else base
    _require(0 <= base <= end <= len(binary), f"Accessor {accessor_index} exceeds BIN chunk")

    values: list[Any] = []
    for item_index in range(count):
        item_offset = base + item_index * stride
        item = struct.unpack_from("<" + fmt * component_count, binary, item_offset)
        values.append(item[0] if component_count == 1 else list(item))
    return values


def _accessor_summary(document: dict[str, Any], accessor_index: int | None) -> dict[str, Any] | None:
    if accessor_index is None:
        return None
    accessors = document.get("accessors", [])
    _require(0 <= accessor_index < len(accessors), f"Invalid accessor index: {accessor_index}")
    accessor = accessors[accessor_index]
    result = {
        "index": accessor_index,
        "count": accessor.get("count", 0),
        "component_type": accessor.get("componentType"),
        "type": accessor.get("type"),
    }
    for key in ("min", "max", "normalized", "byteOffset"):
        if key in accessor:
            result[key] = accessor[key]
    return result


def _build_nodes(document: dict[str, Any]) -> tuple[list[dict[str, Any]], dict[int, int]]:
    raw_nodes = document.get("nodes", [])
    _require(isinstance(raw_nodes, list), "GLB nodes must be an array")
    parents: dict[int, int] = {}
    for parent_index, node in enumerate(raw_nodes):
        _require(isinstance(node, dict), f"Node {parent_index} is not an object")
        for child_index in node.get("children", []):
            _require(isinstance(child_index, int), f"Node {parent_index} has a non-integer child")
            _require(0 <= child_index < len(raw_nodes), f"Node child index out of range: {child_index}")
            _require(child_index not in parents, f"Node has multiple parents: {child_index}")
            parents[child_index] = parent_index

    nodes: list[dict[str, Any]] = []
    for index, node in enumerate(raw_nodes):
        entry: dict[str, Any] = {
            "index": index,
            "name": node.get("name", f"Node_{index}"),
            "parent_index": parents.get(index),
            "children": list(node.get("children", [])),
            "transform": _node_transform(node),
        }
        for key in ("mesh", "skin", "camera", "weights", "extras"):
            if key in node:
                entry[key] = node[key]
        nodes.append(entry)
    return nodes, parents


def _build_skins(
    document: dict[str, Any],
    binary: bytes,
    nodes: list[dict[str, Any]],
    parents: dict[int, int],
) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for skin_index, skin in enumerate(document.get("skins", [])):
        _require(isinstance(skin, dict), f"Skin {skin_index} is not an object")
        joints = skin.get("joints", [])
        _require(isinstance(joints, list), f"Skin {skin_index} joints must be an array")
        joint_entries = []
        for node_index in joints:
            _require(isinstance(node_index, int), f"Skin {skin_index} has a non-integer joint")
            _require(0 <= node_index < len(nodes), f"Skin joint index out of range: {node_index}")
            parent_index = parents.get(node_index)
            joint_entries.append(
                {
                    "node_index": node_index,
                    "name": nodes[node_index]["name"],
                    "parent_index": parent_index,
                    "parent_name": nodes[parent_index]["name"] if parent_index is not None else None,
                }
            )
        entry: dict[str, Any] = {
            "index": skin_index,
            "name": skin.get("name"),
            "skeleton_node_index": skin.get("skeleton"),
            "inverse_bind_matrices_accessor": _accessor_summary(
                document, skin.get("inverseBindMatrices")
            ),
            "joints": joint_entries,
        }
        if "inverseBindMatrices" in skin:
            entry["inverse_bind_matrices"] = _accessor_values(
                document, binary, skin["inverseBindMatrices"]
            )
        result.append(entry)
    return result
def _build_skinning_summaries(
    document: dict[str, Any], binary: bytes, nodes: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    """Aggregate raw skin attributes into factual per-joint summaries."""
    mesh_bindings: dict[int, list[tuple[int, int]]] = {}
    for node in nodes:
        mesh_index = node.get("mesh")
        skin_index = node.get("skin")
        if isinstance(mesh_index, int) and isinstance(skin_index, int):
            mesh_bindings.setdefault(mesh_index, []).append((node["index"], skin_index))

    summaries: list[dict[str, Any]] = []
    for mesh_index, mesh in enumerate(document.get("meshes", [])):
        for primitive_index, primitive in enumerate(mesh.get("primitives", [])):
            attributes = primitive.get("attributes", {})
            position_accessor = attributes.get("POSITION")
            joints_accessor = attributes.get("JOINTS_0")
            weights_accessor = attributes.get("WEIGHTS_0")
            if position_accessor is None or joints_accessor is None or weights_accessor is None:
                continue
            positions = _accessor_values(document, binary, position_accessor)
            joints_values = _accessor_values(document, binary, joints_accessor)
            weights_values = _accessor_values(document, binary, weights_accessor)
            _require(
                len(positions) == len(joints_values) == len(weights_values),
                f"Skin attribute counts differ for mesh {mesh_index} primitive {primitive_index}",
            )

            for node_index, skin_index in mesh_bindings.get(mesh_index, []):
                skins = document.get("skins", [])
                _require(0 <= skin_index < len(skins), f"Invalid node skin index: {skin_index}")
                joint_nodes = skins[skin_index].get("joints", [])
                per_joint: dict[int, dict[str, Any]] = {}
                for vertex, joint_indices, weights in zip(positions, joints_values, weights_values):
                    max_weight = max(weights) if weights else 0.0
                    for joint_index, weight in zip(joint_indices, weights):
                        _require(
                            0 <= joint_index < len(joint_nodes),
                            f"Skin joint index out of range: {joint_index}",
                        )
                        summary = per_joint.setdefault(
                            joint_index,
                            {
                                "joint_index": joint_index,
                                "node_index": joint_nodes[joint_index],
                                "name": nodes[joint_nodes[joint_index]]["name"],
                                "influenced_vertex_count": 0,
                                "dominant_vertex_count": 0,
                                "weight_sum": 0.0,
                                "weighted_position_sum": [0.0, 0.0, 0.0],
                                "bounds_min": [float("inf")] * 3,
                                "bounds_max": [float("-inf")] * 3,
                            },
                        )
                        if weight <= 0.0:
                            continue
                        summary["influenced_vertex_count"] += 1
                        if weight == max_weight:
                            summary["dominant_vertex_count"] += 1
                        summary["weight_sum"] += weight
                        for axis in range(3):
                            summary["weighted_position_sum"][axis] += weight * vertex[axis]
                            summary["bounds_min"][axis] = min(summary["bounds_min"][axis], vertex[axis])
                            summary["bounds_max"][axis] = max(summary["bounds_max"][axis], vertex[axis])

                joints: list[dict[str, Any]] = []
                for joint_index in sorted(per_joint):
                    summary = per_joint[joint_index]
                    weight_sum = summary.pop("weight_sum")
                    position_sum = summary.pop("weighted_position_sum")
                    if weight_sum > 0.0:
                        summary["weighted_centroid"] = [value / weight_sum for value in position_sum]
                    else:
                        summary["weighted_centroid"] = None
                    if summary["influenced_vertex_count"] == 0:
                        summary["bounds_min"] = None
                        summary["bounds_max"] = None
                    joints.append(summary)

                summaries.append(
                    {
                        "mesh_index": mesh_index,
                        "primitive_index": primitive_index,
                        "node_index": node_index,
                        "skin_index": skin_index,
                        "vertex_count": len(positions),
                        "joints": joints,
                    }
                )
    return summaries



def _build_meshes(document: dict[str, Any], binary: bytes) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for mesh_index, mesh in enumerate(document.get("meshes", [])):
        _require(isinstance(mesh, dict), f"Mesh {mesh_index} is not an object")
        primitives = []
        for primitive_index, primitive in enumerate(mesh.get("primitives", [])):
            _require(isinstance(primitive, dict), f"Primitive {primitive_index} is not an object")
            attributes = primitive.get("attributes", {})
            _require(isinstance(attributes, dict), "Primitive attributes must be an object")
            position_accessor = attributes.get("POSITION")
            position_summary = _accessor_summary(document, position_accessor)
            primitive_entry: dict[str, Any] = {
                "index": primitive_index,
                "mode": primitive.get("mode", 4),
                "material": primitive.get("material"),
                "attributes": {
                    name: _accessor_summary(document, accessor_index)
                    for name, accessor_index in attributes.items()
                },
                "indices": _accessor_summary(document, primitive.get("indices")),
                "vertex_count": position_summary["count"] if position_summary else None,
            }
            skin_attributes: dict[str, Any] = {}
            for attribute_name in ("JOINTS_0", "WEIGHTS_0"):
                accessor_index = attributes.get(attribute_name)
                if accessor_index is not None:
                    skin_attributes[attribute_name] = {
                        "accessor": _accessor_summary(document, accessor_index)
                    }
            if skin_attributes:
                primitive_entry["skin_attributes"] = skin_attributes
            primitives.append(primitive_entry)
        result.append(
            {
                "index": mesh_index,
                "name": mesh.get("name"),
                "primitive_count": len(primitives),
                "primitives": primitives,
            }
        )
    return result


def read_glb(path: str | Path) -> dict[str, Any]:
    """Read a GLB into a JSON-serializable factual descriptor."""
    source = Path(path)
    document, binary = _read_glb(source)
    nodes, parents = _build_nodes(document)
    descriptor: dict[str, Any] = {
        "format": "glb",
        "version": 2,
        "source": str(source),
        "asset": document.get("asset", {}),
        "scene": document.get("scene"),
        "scenes": document.get("scenes", []),
        "extensions_used": document.get("extensionsUsed", []),
        "extensions_required": document.get("extensionsRequired", []),
        "nodes": nodes,
        "skins": _build_skins(document, binary, nodes, parents),
        "meshes": _build_meshes(document, binary),
        "skinning": _build_skinning_summaries(document, binary, nodes),
        "facts_only": True,
    }
    return descriptor
