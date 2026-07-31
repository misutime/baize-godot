"""GLTF/GLB/FBX loading and node-parent facts."""
from __future__ import annotations

from pathlib import Path
from typing import Any
from pygltflib import GLTF2

from .fbx_converter import convert_fbx_to_gltf


def load_document(path: str | Path) -> GLTF2:
    source = Path(path)
    suffix = source.suffix.lower()
    if suffix == ".fbx":
        # FBX is converted to a temporary GLB via Blender; the returned
        # document is a plain GLTF2 and the input FBX is never modified.
        return convert_fbx_to_gltf(source)
    if suffix not in {".glb", ".gltf"}:
        raise ValueError(f"Expected .glb, .gltf, or .fbx input, got {source}")
    if not source.is_file():
        raise FileNotFoundError(source)
    return GLTF2().load(str(source))


def build_parent_map(document: GLTF2) -> dict[int, int]:
    parents: dict[int, int] = {}
    nodes = document.nodes or []
    for parent_index, node in enumerate(nodes):
        for child_index in node.children or []:
            if not isinstance(child_index, int) or not 0 <= child_index < len(nodes):
                raise ValueError(f"Node {parent_index} has invalid child index {child_index}")
            if child_index in parents and parents[child_index] != parent_index:
                raise ValueError(f"Node {child_index} has multiple parents")
            parents[child_index] = parent_index
    return parents


def source_facts(document: GLTF2, path: str | Path) -> dict[str, Any]:
    asset = document.asset
    return {
        "path": str(path),
        "format": "glb" if str(path).lower().endswith(".glb") else "gltf",
        "version": getattr(asset, "version", None),
        "generator": getattr(asset, "generator", None),
        "node_count": len(document.nodes or []),
        "skin_count": len(document.skins or []),
        "mesh_count": len(document.meshes or []),
        "animation_count": len(document.animations or []),
    }
