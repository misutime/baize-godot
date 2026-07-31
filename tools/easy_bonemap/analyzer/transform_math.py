"""GLTF transform math with explicit column-major and TRS semantics."""
from __future__ import annotations

from typing import Any
import numpy as np

EPSILON = 1e-8


def _vector(value: Any, size: int) -> np.ndarray | None:
    if not isinstance(value, (list, tuple, np.ndarray)) or len(value) != size:
        return None
    return np.asarray(value, dtype=np.float64)


def identity_matrix() -> np.ndarray:
    return np.eye(4, dtype=np.float64)


def gltf_matrix(values: Any) -> np.ndarray:
    value = _vector(values, 16)
    if value is None:
        raise ValueError("GLTF matrix must contain 16 values")
    return value.reshape((4, 4), order="F")


def quaternion_matrix(rotation: Any) -> np.ndarray:
    value = _vector(rotation, 4)
    if value is None:
        raise ValueError("rotation must contain 4 values")
    length = float(np.linalg.norm(value))
    if length <= EPSILON:
        raise ValueError("rotation quaternion has zero length")
    x, y, z, w = value / length
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ], dtype=np.float64)


def trs_matrix(
    translation: Any = (0.0, 0.0, 0.0),
    rotation: Any = (0.0, 0.0, 0.0, 1.0),
    scale: Any = (1.0, 1.0, 1.0),
) -> np.ndarray:
    position = _vector(translation, 3)
    factors = _vector(scale, 3)
    if position is None or factors is None:
        raise ValueError("translation and scale must contain 3 values")
    result = identity_matrix()
    result[:3, :3] = quaternion_matrix(rotation) @ np.diag(factors)
    result[:3, 3] = position
    return result


def _node_values(node: Any, name: str, default: tuple[float, ...]) -> Any:
    value = getattr(node, name, None)
    if value is None or (isinstance(value, (list, tuple, np.ndarray)) and len(value) == 0):
        return default
    return value



def local_matrix(node: Any) -> np.ndarray:
    """Return node.matrix when present, otherwise compose GLTF TRS."""
    matrix = getattr(node, "matrix", None)
    if isinstance(matrix, (list, tuple, np.ndarray)) and len(matrix) == 16:
        return gltf_matrix(matrix)
    return trs_matrix(
        _node_values(node, "translation", (0.0, 0.0, 0.0)),
        _node_values(node, "rotation", (0.0, 0.0, 0.0, 1.0)),
        _node_values(node, "scale", (1.0, 1.0, 1.0)),
    )


def world_matrices(document: Any) -> dict[int, np.ndarray]:
    """Evaluate every node world matrix through the complete parent chain."""
    parents: dict[int, int] = {}
    nodes = document.nodes or []
    for parent_index, node in enumerate(nodes):
        for child_index in node.children or []:
            if not isinstance(child_index, (int, np.integer)) or not 0 <= int(child_index) < len(nodes):
                raise ValueError(f"Invalid child index {child_index}")
            child_index = int(child_index)
            if child_index in parents and parents[child_index] != parent_index:
                raise ValueError(f"Node {child_index} has multiple parents")
            parents[child_index] = parent_index
    cache: dict[int, np.ndarray] = {}
    active: set[int] = set()

    def evaluate(index: int) -> np.ndarray:
        if index in cache:
            return cache[index]
        if index in active:
            raise ValueError(f"Transform cycle at node {index}")
        active.add(index)
        local = local_matrix(nodes[index])
        parent = parents.get(index)
        result = local if parent is None else evaluate(parent) @ local
        active.remove(index)
        cache[index] = result
        return result

    for index in range(len(nodes)):
        evaluate(index)
    return cache


def decompose_matrix(matrix: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    position = matrix[:3, 3].copy()
    basis = matrix[:3, :3].copy()
    scale = np.linalg.norm(basis, axis=0)
    if np.any(scale <= EPSILON):
        raise ValueError("Transform has a zero-length basis axis")
    rotation = basis / scale
    if np.linalg.det(rotation) < 0:
        axis = int(np.argmax(np.abs(scale)))
        scale[axis] *= -1
        rotation[:, axis] *= -1
    trace = float(np.trace(rotation))
    if trace > 0:
        root = np.sqrt(trace + 1) * 2
        quaternion = np.array([
            (rotation[2, 1] - rotation[1, 2]) / root,
            (rotation[0, 2] - rotation[2, 0]) / root,
            (rotation[1, 0] - rotation[0, 1]) / root,
            0.25 * root,
        ])
    else:
        index = int(np.argmax(np.diag(rotation)))
        if index == 0:
            root = np.sqrt(max(1 + rotation[0, 0] - rotation[1, 1] - rotation[2, 2], 0)) * 2
            quaternion = np.array([0.25 * root, (rotation[0, 1] + rotation[1, 0]) / root, (rotation[0, 2] + rotation[2, 0]) / root, (rotation[2, 1] - rotation[1, 2]) / root])
        elif index == 1:
            root = np.sqrt(max(1 + rotation[1, 1] - rotation[0, 0] - rotation[2, 2], 0)) * 2
            quaternion = np.array([(rotation[0, 1] + rotation[1, 0]) / root, 0.25 * root, (rotation[1, 2] + rotation[2, 1]) / root, (rotation[0, 2] - rotation[2, 0]) / root])
        else:
            root = np.sqrt(max(1 + rotation[2, 2] - rotation[0, 0] - rotation[1, 1], 0)) * 2
            quaternion = np.array([(rotation[0, 2] + rotation[2, 0]) / root, (rotation[1, 2] + rotation[2, 1]) / root, 0.25 * root, (rotation[1, 0] - rotation[0, 1]) / root])
    return position, quaternion / np.linalg.norm(quaternion), scale


def unit_vector(value: np.ndarray) -> np.ndarray | None:
    length = float(np.linalg.norm(value))
    return None if length <= EPSILON else value / length


def json_vector(value: np.ndarray | None) -> list[float] | None:
    return None if value is None else [round(float(item), 8) for item in value]
