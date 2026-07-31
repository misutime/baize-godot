"""Structural analysis of a factual skeleton descriptor.

No anatomical or humanoid semantics are inferred here. The output only
summarizes graph shape so a later stage can inspect or classify it.
"""

from __future__ import annotations

from typing import Any


def analyze_skeleton_graph(descriptor: dict[str, Any]) -> dict[str, Any]:
    nodes = descriptor.get("nodes", [])
    by_index = {node["index"]: node for node in nodes}
    depths: dict[int, int] = {}

    def depth(index: int) -> int:
        if index in depths:
            return depths[index]
        parent = by_index[index].get("parent_index")
        depths[index] = 0 if parent is None else depth(parent) + 1
        return depths[index]

    for index in by_index:
        depth(index)

    roots = [node for node in nodes if node.get("parent_index") is None]
    branch_nodes = [
        {
            "index": node["index"],
            "name": node["name"],
            "parent_index": node.get("parent_index"),
            "children": list(node.get("children", [])),
            "child_count": len(node.get("children", [])),
            "depth": depths[node["index"]],
        }
        for node in nodes
        if len(node.get("children", [])) > 1
    ]

    return {
        "node_count": len(nodes),
        "root_nodes": [
            {"index": node["index"], "name": node["name"]} for node in roots
        ],
        "leaf_count": sum(not node.get("children") for node in nodes),
        "branch_nodes": branch_nodes,
        "max_depth": max(depths.values(), default=0),
        "facts_only": True,
    }
