"""Behavior tests for the phase-1 Normalized Skeleton Graph.

The suite exercises the locked phase-1 contract end to end:

* ``analyzer.transform_math``:
    - ``trs_matrix(translation, rotation, scale)`` -> 4x4 numpy array
    - ``local_matrix(node)`` -> 4x4 numpy array; ``node.matrix`` wins over TRS
    - ``world_matrices(document)`` -> ``{node_index: 4x4 numpy array}``
* ``analyzer.skeleton_graph.extract_normalized_skeleton_graph(document, source="")``:
    - report format ``easy_bonemap.normalized_skeleton_graph.v1``
    - bones are exactly the union of ``skins[].joints``; ordinary nodes are excluded
    - joint-subset parent links, multiple roots supported, no fabricated root edge
    - world positions honor parent rotation and non-uniform scale; ``matrix``
      takes precedence over TRS
    - normalization is invariant to a global translation and uniform scale
    - degenerate inputs (no skins, empty joints, out-of-range joint) surface as
      ``degeneracies``/``warnings`` instead of crashing

Inputs are built entirely in memory as ``pygltflib.GLTF2`` documents; no
filesystem fixtures are used.

Run directly:
    python tools/easy_bonemap/tests/test_skeleton_graph.py
or from the easy_bonemap directory:
    python -m unittest discover -s tests
"""

from __future__ import annotations

import copy
import json
import os
import sys
import unittest
from typing import Any, Sequence

import numpy as np

# The analyzer package lives one directory up (tools/easy_bonemap).
_ANALYZER_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _ANALYZER_ROOT not in sys.path:
    sys.path.insert(0, _ANALYZER_ROOT)

import pygltflib as gltf  # noqa: E402

from analyzer import transform_math  # noqa: E402
from analyzer.skeleton_graph import extract_normalized_skeleton_graph  # noqa: E402

REPORT_FORMAT = "easy_bonemap.normalized_skeleton_graph.v1"
REQUIRED_REPORT_KEYS = {
    "format",
    "source",
    "bone_count",
    "bones",
    "roots",
    "degeneracies",
    "warnings",
}
REQUIRED_BONE_KEYS = {
    "index",
    "name",
    "parent",
    "local",
    "world_position",
    "normalized_position",
}

# (x, y, z, w) quaternion for +90 degrees about +Y.
ROT_Y_90 = (0.0, 0.7071067811865476, 0.0, 0.7071067811865476)


# ---------------------------------------------------------------------------
# Fixtures: in-memory pygltflib documents (plus one plain-dict fact object).
# ---------------------------------------------------------------------------


def gltf_node(
    name: str,
    translation: Sequence[float] | None = None,
    rotation: Sequence[float] | None = None,
    scale: Sequence[float] | None = None,
    matrix: Sequence[float] | None = None,
    children: Sequence[int] | None = None,
) -> gltf.Node:
    node = gltf.Node(name=name)
    if translation is not None:
        node.translation = list(translation)
    if rotation is not None:
        node.rotation = list(rotation)
    if scale is not None:
        node.scale = list(scale)
    if matrix is not None:
        node.matrix = list(matrix)
    if children is not None:
        node.children = list(children)
    return node


def make_document(
    nodes: list[gltf.Node],
    joint_lists: list[list[int]],
) -> gltf.GLTF2:
    """One in-memory GLTF2; each entry of ``joint_lists`` becomes a skin."""
    skins = [
        gltf.Skin(name=f"Skin{index}", joints=list(joints))
        for index, joints in enumerate(joint_lists)
    ]
    return gltf.GLTF2(
        asset=gltf.Asset(version="2.0", generator="synthetic"),
        nodes=nodes,
        skins=skins,
    )


def chain_document(
    n: int,
    translations: list[tuple[float, float, float]],
    rotations: list[Sequence[float] | None] | None = None,
    scales: list[Sequence[float] | None] | None = None,
    joint_indices: list[int] | None = None,
) -> gltf.GLTF2:
    """A straight chain node 0 -> 1 -> ... -> n-1 with the given local TRS."""
    nodes = [gltf_node(f"B{index}", translation=translations[index]) for index in range(n)]
    for index in range(1, n):
        nodes[index - 1].children.append(index)
    if rotations is not None:
        for index, rotation in enumerate(rotations):
            if rotation is not None:
                nodes[index].rotation = list(rotation)
    if scales is not None:
        for index, scale in enumerate(scales):
            if scale is not None:
                nodes[index].scale = list(scale)
    return make_document(nodes, [joint_indices if joint_indices is not None else list(range(n))])


# ---------------------------------------------------------------------------
# analyzer.transform_math
# ---------------------------------------------------------------------------


class TransformMathTest(unittest.TestCase):
    """Direct math contract of ``analyzer.transform_math``."""

    def test_trs_identity_by_default(self) -> None:
        np.testing.assert_allclose(transform_math.trs_matrix(), np.eye(4), atol=1e-12)

    def test_trs_translation_only(self) -> None:
        matrix = transform_math.trs_matrix(translation=(1.0, 2.0, 3.0))
        point = matrix @ np.array([0.0, 0.0, 0.0, 1.0])
        np.testing.assert_allclose(point, [1.0, 2.0, 3.0, 1.0], atol=1e-9)

    def test_trs_rotation_about_y_maps_z_to_x(self) -> None:
        matrix = transform_math.trs_matrix(rotation=ROT_Y_90)
        point = matrix @ np.array([0.0, 0.0, 1.0, 1.0])
        np.testing.assert_allclose(point, [1.0, 0.0, 0.0, 1.0], atol=1e-9)

    def test_trs_non_uniform_scale(self) -> None:
        matrix = transform_math.trs_matrix(scale=(2.0, 1.0, 0.5))
        point = matrix @ np.array([1.0, 1.0, 1.0, 1.0])
        np.testing.assert_allclose(point, [2.0, 1.0, 0.5, 1.0], atol=1e-9)

    def test_trs_applies_rotation_before_translation(self) -> None:
        # TRS order: rotate the point, then translate it.
        matrix = transform_math.trs_matrix(translation=(1.0, 0.0, 0.0), rotation=ROT_Y_90)
        rotated = matrix @ np.array([0.0, 0.0, 1.0, 1.0])
        np.testing.assert_allclose(rotated, [2.0, 0.0, 0.0, 1.0], atol=1e-9)
        origin = matrix @ np.array([0.0, 0.0, 0.0, 1.0])
        np.testing.assert_allclose(origin, [1.0, 0.0, 0.0, 1.0], atol=1e-9)

    def test_trs_own_scale_does_not_scale_translation(self) -> None:
        matrix = transform_math.trs_matrix(translation=(1.0, 2.0, 3.0), scale=(5.0, 5.0, 5.0))
        origin = matrix @ np.array([0.0, 0.0, 0.0, 1.0])
        np.testing.assert_allclose(origin, [1.0, 2.0, 3.0, 1.0], atol=1e-9)

    def test_local_matrix_from_trs_only(self) -> None:
        node = gltf.Node(translation=[1.0, 2.0, 3.0], rotation=list(ROT_Y_90))
        np.testing.assert_allclose(
            transform_math.local_matrix(node),
            transform_math.trs_matrix(translation=(1.0, 2.0, 3.0), rotation=ROT_Y_90),
            atol=1e-12,
        )

    def test_local_matrix_from_matrix_only(self) -> None:
        matrix = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 5.0, 6.0, 7.0, 1.0]
        node = gltf.Node(matrix=matrix)
        expected = np.array(matrix, dtype=float).reshape(4, 4).T  # column-major storage
        np.testing.assert_allclose(transform_math.local_matrix(node), expected, atol=1e-12)

    def test_local_matrix_matrix_takes_precedence_over_trs(self) -> None:
        matrix = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 5.0, 6.0, 7.0, 1.0]
        node = gltf.Node(matrix=matrix, translation=[1.0, 2.0, 3.0], scale=[3.0, 3.0, 3.0])
        expected = np.array(matrix, dtype=float).reshape(4, 4).T
        np.testing.assert_allclose(transform_math.local_matrix(node), expected, atol=1e-12)

    def test_world_matrices_keys_and_root_identity(self) -> None:
        document = chain_document(2, [(0.0, 0.0, 0.0), (0.0, 1.0, 0.0)])
        worlds = transform_math.world_matrices(document)
        self.assertEqual(set(worlds.keys()), {0, 1})
        np.testing.assert_allclose(worlds[0], np.eye(4), atol=1e-12)
        # Column-vector convention: the translation lives in the last column.
        np.testing.assert_allclose(
            worlds[1],
            np.array([[1, 0, 0, 0], [0, 1, 0, 1], [0, 0, 1, 0], [0, 0, 0, 1]], dtype=float),
            atol=1e-9,
        )

    def test_world_matrices_apply_parent_rotation(self) -> None:
        document = chain_document(
            2,
            [(0.0, 0.0, 0.0), (0.0, 0.0, 1.0)],
            rotations=[ROT_Y_90, None],
        )
        worlds = transform_math.world_matrices(document)
        child_origin = worlds[1] @ np.array([0.0, 0.0, 0.0, 1.0])
        np.testing.assert_allclose(child_origin[:3], [1.0, 0.0, 0.0], atol=1e-9)

    def test_world_matrices_apply_parent_non_uniform_scale(self) -> None:
        document = chain_document(
            2,
            [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0)],
            scales=[(2.0, 1.0, 0.5), None],
        )
        worlds = transform_math.world_matrices(document)
        child_origin = worlds[1] @ np.array([0.0, 0.0, 0.0, 1.0])
        np.testing.assert_allclose(child_origin[:3], [2.0, 0.0, 0.0], atol=1e-9)

    def test_world_matrices_cover_every_node(self) -> None:
        nodes = [
            gltf_node("R", children=[1, 2]),
            gltf_node("C1", translation=(0.0, 1.0, 0.0)),
            gltf_node("C2", translation=(1.0, 0.0, 0.0)),
        ]
        document = make_document(nodes, [[0]])
        worlds = transform_math.world_matrices(document)
        self.assertEqual(set(worlds.keys()), {0, 1, 2})


# ---------------------------------------------------------------------------
# extract_normalized_skeleton_graph: report contract
# ---------------------------------------------------------------------------


class ReportContractTest(unittest.TestCase):
    def setUp(self) -> None:
        self.document = chain_document(3, [(0.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 1.0, 0.0)])
        self.report = extract_normalized_skeleton_graph(self.document, source="synthetic://chain.glb")

    def test_format_and_required_keys(self) -> None:
        self.assertEqual(self.report["format"], REPORT_FORMAT)
        missing = REQUIRED_REPORT_KEYS - self.report.keys()
        self.assertEqual(missing, set(), f"missing report keys: {missing}")

    def test_report_is_json_serializable(self) -> None:
        round_trip = json.loads(json.dumps(self.report))
        self.assertEqual(round_trip, self.report)

    def test_source_default_and_passthrough(self) -> None:
        default = extract_normalized_skeleton_graph(self.document)
        self.assertEqual(default["source"], "")
        explicit = extract_normalized_skeleton_graph(self.document, source="file://other.glb")
        self.assertEqual(explicit["source"], "file://other.glb")
        self.assertEqual(self.report["source"], "synthetic://chain.glb")

    def test_deterministic_and_input_not_mutated(self) -> None:
        snapshot = copy.deepcopy(self.document.to_dict())
        first = extract_normalized_skeleton_graph(self.document)
        second = extract_normalized_skeleton_graph(self.document)
        self.assertEqual(first, second)
        self.assertEqual(self.document.to_dict(), snapshot)

    def test_bone_entries_have_required_keys(self) -> None:
        self.assertGreaterEqual(self.report["bone_count"], 1)
        self.assertEqual(len(self.report["bones"]), self.report["bone_count"])
        for bone in self.report["bones"]:
            missing = REQUIRED_BONE_KEYS - bone.keys()
            self.assertEqual(missing, set(), f"bone {bone['index']} missing {missing}")
            self.assertIsInstance(bone["local"], dict)
            self.assertEqual(len(bone["world_position"]), 3)
            self.assertEqual(len(bone["normalized_position"]), 3)

    def test_warnings_and_degeneracies_are_lists(self) -> None:
        self.assertIsInstance(self.report["warnings"], list)
        self.assertIsInstance(self.report["degeneracies"], list)

    def test_accepts_minimal_gltf2_object(self) -> None:
        # A bare in-memory GLTF2 (no asset/scenes) is a valid fact object.
        document = gltf.GLTF2(
            nodes=[
                gltf.Node(name="R", children=[1]),
                gltf.Node(name="C", translation=[0.0, 1.0, 0.0]),
            ],
            skins=[gltf.Skin(name="S", joints=[0, 1])],
        )
        report = extract_normalized_skeleton_graph(document, source="synthetic://minimal.glb")
        self.assertEqual(report["format"], REPORT_FORMAT)
        self.assertEqual(report["source"], "synthetic://minimal.glb")
        self.assertEqual(report["bone_count"], 2)
        positions = {bone["index"]: bone["world_position"] for bone in report["bones"]}
        self.assertAlmostEqual(float(positions[1][1]), 1.0, places=5)


# ---------------------------------------------------------------------------
# Bone selection: skins[].joints only, ordinary nodes excluded
# ---------------------------------------------------------------------------


class BoneSelectionTest(unittest.TestCase):
    def test_bones_are_exactly_the_skin_joints(self) -> None:
        document = chain_document(4, [(0.0, 0.0, 0.0)] * 4, joint_indices=[0, 2, 3])
        report = extract_normalized_skeleton_graph(document)
        self.assertEqual(report["bone_count"], 3)
        self.assertEqual({bone["index"] for bone in report["bones"]}, {0, 2, 3})

    def test_ordinary_nodes_excluded(self) -> None:
        nodes = chain_document(3, [(0.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 1.0, 0.0)]).nodes
        nodes[2].mesh = 0  # a mesh node that is not a joint
        document = make_document(nodes, [[0, 1]])
        report = extract_normalized_skeleton_graph(document)
        self.assertEqual(report["bone_count"], 2)
        self.assertEqual({bone["index"] for bone in report["bones"]}, {0, 1})

    def test_multiple_skins_union_with_dedup(self) -> None:
        document = chain_document(4, [(0.0, 0.0, 0.0)] * 4)
        document.skins = [
            gltf.Skin(name="A", joints=[0, 1]),
            gltf.Skin(name="B", joints=[1, 2, 3]),  # index 1 repeated across skins
        ]
        report = extract_normalized_skeleton_graph(document)
        self.assertEqual(report["bone_count"], 4)
        self.assertEqual({bone["index"] for bone in report["bones"]}, {0, 1, 2, 3})

    def test_no_skins_is_degenerate(self) -> None:
        document = make_document(chain_document(2, [(0.0, 0.0, 0.0), (0.0, 1.0, 0.0)]).nodes, [])
        report = extract_normalized_skeleton_graph(document)
        self.assertEqual(report["bone_count"], 0)
        self.assertEqual(report["bones"], [])
        self.assertTrue(report["degeneracies"], "document without skins must be flagged")

    def test_empty_joints_is_degenerate(self) -> None:
        document = make_document(chain_document(2, [(0.0, 0.0, 0.0), (0.0, 1.0, 0.0)]).nodes, [[]])
        report = extract_normalized_skeleton_graph(document)
        self.assertEqual(report["bone_count"], 0)
        self.assertTrue(report["degeneracies"], "skin with empty joints must be flagged")

    def test_out_of_range_joint_is_degenerate(self) -> None:
        document = make_document(chain_document(2, [(0.0, 0.0, 0.0), (0.0, 1.0, 0.0)]).nodes, [[0, 5]])
        report = extract_normalized_skeleton_graph(document)
        self.assertEqual(report["bone_count"], 1)
        self.assertEqual([bone["index"] for bone in report["bones"]], [0])
        self.assertTrue(report["degeneracies"], "out-of-range joint must be flagged")


# ---------------------------------------------------------------------------
# Skeleton hierarchy: joint-subset parents, multiple roots, no fabricated edges
# ---------------------------------------------------------------------------


class HierarchyTest(unittest.TestCase):
    def test_parent_links_follow_chain(self) -> None:
        document = chain_document(4, [(0.0, 0.0, 0.0)] * 4)
        report = extract_normalized_skeleton_graph(document)
        parents = {bone["index"]: bone["parent"] for bone in report["bones"]}
        self.assertEqual(parents, {0: -1, 1: 0, 2: 1, 3: 2})

    def test_parent_links_only_reference_joints(self) -> None:
        # Node 1 is not a joint: joint 2's immediate parent is not a bone, so no
        # edge is fabricated through the non-joint node and joint 2 becomes a root.
        document = chain_document(4, [(0.0, 0.0, 0.0)] * 4, joint_indices=[0, 2, 3])
        report = extract_normalized_skeleton_graph(document)
        parents = {bone["index"]: bone["parent"] for bone in report["bones"]}
        self.assertEqual(parents, {0: -1, 2: -1, 3: 2})
        joint_set = {0, 2, 3}
        for bone in report["bones"]:
            self.assertTrue(bone["parent"] == -1 or bone["parent"] in joint_set)
        self.assertEqual(sorted(report["roots"]), [0, 2])

    def test_multiple_roots_supported(self) -> None:
        nodes = [
            gltf_node("R1", translation=(0.0, 0.0, 0.0), children=[1]),
            gltf_node("C1", translation=(0.0, 1.0, 0.0)),
            gltf_node("R2", translation=(2.0, 0.0, 0.0), children=[3]),
            gltf_node("C2", translation=(0.0, 1.0, 0.0)),
        ]
        report = extract_normalized_skeleton_graph(make_document(nodes, [[0, 1, 2, 3]]))
        self.assertEqual(sorted(report["roots"]), [0, 2])
        for bone in report["bones"]:
            if bone["index"] in (0, 2):
                self.assertEqual(bone["parent"], -1, bone["index"])

    def test_roots_have_no_fabricated_edge_or_length(self) -> None:
        document = chain_document(2, [(0.0, 0.0, 0.0), (0.0, 1.0, 0.0)])
        report = extract_normalized_skeleton_graph(document)
        self.assertEqual(report["roots"], [0])
        root = next(bone for bone in report["bones"] if bone["index"] == 0)
        self.assertEqual(root["parent"], -1)
        self.assertEqual(report["bone_count"], 2)  # nothing invented above the root

    def test_branching_children_allowed(self) -> None:
        nodes = [
            gltf_node("Hips", translation=(0.0, 0.0, 0.0), children=[1, 2]),
            gltf_node("Left", translation=(-0.3, 0.0, 0.0)),
            gltf_node("Right", translation=(0.3, 0.0, 0.0)),
        ]
        report = extract_normalized_skeleton_graph(make_document(nodes, [[0, 1, 2]]))
        parents = {bone["index"]: bone["parent"] for bone in report["bones"]}
        self.assertEqual(parents, {0: -1, 1: 0, 2: 0})


# ---------------------------------------------------------------------------
# Geometry: world positions from TRS/matrix composition
# ---------------------------------------------------------------------------


class GeometryTest(unittest.TestCase):
    def assert_vec(self, actual: Sequence[float], expected: Sequence[float], places: int = 5) -> None:
        self.assertEqual(len(actual), 3)
        for got, want in zip(actual, expected):
            self.assertAlmostEqual(float(got), want, places=places)

    def positions(self, document: gltf.GLTF2) -> dict[int, list[float]]:
        report = extract_normalized_skeleton_graph(document)
        return {bone["index"]: bone["world_position"] for bone in report["bones"]}

    def test_world_positions_straight_chain(self) -> None:
        document = chain_document(3, [(0.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 1.0, 0.0)])
        positions = self.positions(document)
        self.assert_vec(positions[0], (0.0, 0.0, 0.0))
        self.assert_vec(positions[1], (0.0, 1.0, 0.0))
        self.assert_vec(positions[2], (0.0, 2.0, 0.0))

    def test_world_position_under_parent_rotation(self) -> None:
        nodes = [
            gltf_node("Root", rotation=ROT_Y_90, children=[1]),
            gltf_node("Child", translation=(0.0, 0.0, 1.0)),
        ]
        positions = self.positions(make_document(nodes, [[0, 1]]))
        self.assert_vec(positions[1], (1.0, 0.0, 0.0))

    def test_world_position_under_parent_non_uniform_scale(self) -> None:
        nodes = [
            gltf_node("Root", scale=(2.0, 1.0, 0.5), children=[1]),
            gltf_node("Child", translation=(1.0, 0.0, 0.0)),
        ]
        positions = self.positions(make_document(nodes, [[0, 1]]))
        self.assert_vec(positions[1], (2.0, 0.0, 0.0))

    def test_world_position_rotation_then_scale_then_child(self) -> None:
        nodes = [
            gltf_node("Root", rotation=ROT_Y_90, scale=(2.0, 2.0, 2.0), children=[1]),
            gltf_node("Child", translation=(0.0, 0.0, 1.0)),
        ]
        positions = self.positions(make_document(nodes, [[0, 1]]))
        self.assert_vec(positions[1], (2.0, 0.0, 0.0))

    def test_default_node_transform_is_identity(self) -> None:
        nodes = [gltf_node("Bare", children=[1]), gltf_node("Child", translation=(0.0, 1.0, 0.0))]
        positions = self.positions(make_document(nodes, [[0, 1]]))
        self.assert_vec(positions[0], (0.0, 0.0, 0.0))
        self.assert_vec(positions[1], (0.0, 1.0, 0.0))

    def test_matrix_precedence_in_report(self) -> None:
        matrix = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 5.0, 6.0, 7.0, 1.0]
        node = gltf_node("M", matrix=matrix, translation=(1.0, 2.0, 3.0))
        report = extract_normalized_skeleton_graph(make_document([node], [[0]]))
        bone = report["bones"][0]
        self.assert_vec(bone["world_position"], (5.0, 6.0, 7.0))
        self.assertEqual(bone["local"], {"matrix": matrix})

    def test_trs_reported_when_no_matrix(self) -> None:
        node = gltf_node("T", translation=(1.0, 2.0, 3.0))
        report = extract_normalized_skeleton_graph(make_document([node], [[0]]))
        bone = report["bones"][0]
        self.assert_vec(bone["world_position"], (1.0, 2.0, 3.0))
        self.assertIn("translation", bone["local"])
        self.assertNotIn("matrix", bone["local"])

    def test_report_matches_transform_math(self) -> None:
        nodes = [
            gltf_node("Root", rotation=ROT_Y_90, scale=(2.0, 1.0, 0.5), children=[1]),
            gltf_node("Child", translation=(0.0, 0.0, 1.0)),
        ]
        document = make_document(nodes, [[0, 1]])
        report = extract_normalized_skeleton_graph(document)
        worlds = transform_math.world_matrices(document)
        expected = worlds[1] @ np.array([0.0, 0.0, 0.0, 1.0])
        bone = next(bone for bone in report["bones"] if bone["index"] == 1)
        np.testing.assert_allclose(bone["world_position"], expected[:3], atol=1e-6)


# ---------------------------------------------------------------------------
# Normalization: invariant to global translation and uniform scale
# ---------------------------------------------------------------------------


class NormalizationTest(unittest.TestCase):
    def assert_norm_positions_equal(
        self,
        report_a: dict[str, Any],
        report_b: dict[str, Any],
        places: int = 9,
    ) -> None:
        values_a = [bone["normalized_position"] for bone in report_a["bones"]]
        values_b = [bone["normalized_position"] for bone in report_b["bones"]]
        self.assertEqual(len(values_a), len(values_b))
        for position_a, position_b in zip(values_a, values_b):
            for value_a, value_b in zip(position_a, position_b):
                self.assertAlmostEqual(float(value_a), float(value_b), places=places)

    def test_translation_invariance(self) -> None:
        base = chain_document(3, [(0.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 1.0, 0.0)])
        report_base = extract_normalized_skeleton_graph(base, source="base")
        shifted = chain_document(3, [(10.0, -5.0, 2.0), (0.0, 1.0, 0.0), (0.0, 1.0, 0.0)])
        report_shifted = extract_normalized_skeleton_graph(shifted, source="shifted")
        self.assert_norm_positions_equal(report_base, report_shifted)
        # World positions DO move with the translation.
        world_base = [bone["world_position"] for bone in report_base["bones"]]
        world_shifted = [bone["world_position"] for bone in report_shifted["bones"]]
        self.assertNotEqual(world_base, world_shifted)

    def test_uniform_scale_invariance(self) -> None:
        base = chain_document(3, [(0.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 1.0, 0.0)])
        report_base = extract_normalized_skeleton_graph(base, source="base")
        scaled = chain_document(3, [(0.0, 0.0, 0.0), (0.0, 3.0, 0.0), (0.0, 3.0, 0.0)])
        report_scaled = extract_normalized_skeleton_graph(scaled, source="scaled")
        self.assert_norm_positions_equal(report_base, report_scaled)

    def test_translation_and_uniform_scale_invariance(self) -> None:
        base = chain_document(3, [(0.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 1.0, 0.0)])
        report_base = extract_normalized_skeleton_graph(base, source="base")
        transformed = chain_document(3, [(4.0, 1.0, -2.0), (0.0, 2.0, 0.0), (0.0, 2.0, 0.0)])
        report_transformed = extract_normalized_skeleton_graph(transformed, source="transformed")
        self.assert_norm_positions_equal(report_base, report_transformed)

    def test_non_uniform_relative_shape_changes_normalized_output(self) -> None:
        base = chain_document(3, [(0.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 1.0, 0.0)])
        report_base = extract_normalized_skeleton_graph(base, source="base")
        stretched = chain_document(3, [(0.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 2.0, 0.0)])
        report_stretched = extract_normalized_skeleton_graph(stretched, source="stretched")
        self.assertNotEqual(
            [bone["normalized_position"] for bone in report_base["bones"]],
            [bone["normalized_position"] for bone in report_stretched["bones"]],
        )


# ---------------------------------------------------------------------------
# Names are provenance only: they never change geometry
# ---------------------------------------------------------------------------


class NameProvenanceTest(unittest.TestCase):
    def test_names_do_not_affect_geometry(self) -> None:
        def build(names: list[str]) -> gltf.GLTF2:
            nodes = [
                gltf_node(names[0], rotation=ROT_Y_90, scale=(2.0, 1.0, 0.5), children=[1]),
                gltf_node(names[1], translation=(0.0, 0.0, 1.0)),
                gltf_node(names[2], translation=(0.0, 1.0, 0.0)),
            ]
            return make_document(nodes, [[0, 1, 2]])

        report_a = extract_normalized_skeleton_graph(build(["Root", "Child", "Leaf"]), source="a")
        report_b = extract_normalized_skeleton_graph(build(["Xyzzy", "Qux", "Foo"]), source="b")
        for key in ("parent", "local", "world_position", "normalized_position"):
            values_a = [bone[key] for bone in report_a["bones"]]
            values_b = [bone[key] for bone in report_b["bones"]]
            self.assertEqual(values_a, values_b, key)
        self.assertEqual(report_a["roots"], report_b["roots"])
        # Names remain available as provenance.
        self.assertEqual([bone["name"] for bone in report_a["bones"]], ["Root", "Child", "Leaf"])
        self.assertEqual([bone["name"] for bone in report_b["bones"]], ["Xyzzy", "Qux", "Foo"])


if __name__ == "__main__":
    unittest.main()
