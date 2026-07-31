"""Deterministic contract tests for EasyBoneMap candidate generation.

The tests exercise ``generate_body_candidates`` against synthetic, fully
JSON-serializable factual descriptors (same shape as ``analyzer.glb_reader``
produces for real GLBs). They never depend on filesystem assets, and they
verify observable behavior only: report keys/values, slot coverage and order,
candidate shape and score bounds, determinism, and the "no invented mapping"
rule when semantic evidence is absent.

Run directly:
    python tools/easy_bonemap/tests/test_candidate_generation.py
or from the easy_bonemap directory:
    python -m unittest discover -s tests
"""

from __future__ import annotations

import copy
import json
import os
import sys
import unittest
from typing import Any

# The analyzer package lives one directory up (tools/easy_bonemap).
_ANALYZER_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _ANALYZER_ROOT not in sys.path:
    sys.path.insert(0, _ANALYZER_ROOT)

from analyzer.candidate_generation import generate_body_candidates  # noqa: E402

PROFILE = "SkeletonProfileHumanoid"
SCOPE = "body_and_limbs"
FORMAT = "easy_bonemap.body_candidates.v1"

# Fixed contract: body scope has exactly these 18 profile bones, in this order.
BODY_SLOTS = [
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
]

ALLOWED_STATUS = {"candidate", "unknown", "ambiguous"}
REQUIRED_REPORT_KEYS = {"format", "source", "asset", "profile", "scope", "slots", "warnings"}
REQUIRED_SLOT_KEYS = {"profile_bone", "status", "candidates"}
REQUIRED_CANDIDATE_KEYS = {"skeleton_bone", "score", "features", "evidence"}
# Keys that would constitute a claimed final mapping rather than a proposal.
FINAL_MAPPING_KEYS = {"mapping", "final_mapping", "assignments"}
FINAL_FLAG_KEYS = {"selected", "assigned"}

# (name, parent_name, world_position) for a symmetric, unambiguously humanoid
# skeleton: spine rises on +Y, arms branch laterally from the chest, legs hang
# from the hips, everything mirrored across X=0.
HUMANOID_ENTRIES = [
    ("Root", None, (0.0, 0.00, 0.00)),
    ("Hips", "Root", (0.0, 0.90, 0.00)),
    ("Spine", "Hips", (0.0, 1.00, 0.00)),
    ("Chest", "Spine", (0.0, 1.15, 0.00)),
    ("Neck", "Chest", (0.0, 1.30, 0.00)),
    ("Head", "Neck", (0.0, 1.40, 0.00)),
    ("LeftUpperArm", "Chest", (-0.25, 1.20, 0.00)),
    ("LeftLowerArm", "LeftUpperArm", (-0.50, 1.20, 0.00)),
    ("LeftHand", "LeftLowerArm", (-0.70, 1.20, 0.00)),
    ("RightUpperArm", "Chest", (0.25, 1.20, 0.00)),
    ("RightLowerArm", "RightUpperArm", (0.50, 1.20, 0.00)),
    ("RightHand", "RightLowerArm", (0.70, 1.20, 0.00)),
    ("LeftUpperLeg", "Hips", (-0.10, 0.55, 0.00)),
    ("LeftLowerLeg", "LeftUpperLeg", (-0.10, 0.15, 0.00)),
    ("LeftFoot", "LeftLowerLeg", (-0.10, 0.05, 0.05)),
    ("RightUpperLeg", "Hips", (0.10, 0.55, 0.00)),
    ("RightLowerLeg", "RightUpperLeg", (0.10, 0.15, 0.00)),
    ("RightFoot", "RightLowerLeg", (0.10, 0.05, 0.05)),
]


def build_facts(
    entries: list[tuple[str, str | None, tuple[float, float, float]]],
    source: str = "synthetic://humanoid.glb",
    with_skinning: bool = True,
) -> dict[str, Any]:
    """Build a factual descriptor in the exact shape of ``analyzer.glb_reader``.

    ``entries`` are (name, parent_name, world_position) triples. Relative
    translations are derived from the world positions, so parents/children,
    transforms, skin joints, and skinning summaries are all mutually
    consistent. The result is JSON-serializable.
    """
    world: dict[str, tuple[float, float, float]] = {}
    for name, _parent, position in entries:
        world[name] = position
    index_of: dict[str, int] = {name: index for index, (name, _, _) in enumerate(entries)}

    children: dict[int, list[int]] = {index: [] for index in range(len(entries))}
    nodes: list[dict[str, Any]] = []
    for index, (name, parent, position) in enumerate(entries):
        if parent is not None:
            children[index_of[parent]].append(index)
            parent_position = world[parent]
            translation = [position[axis] - parent_position[axis] for axis in range(3)]
        else:
            translation = list(position)
        nodes.append(
            {
                "index": index,
                "name": name,
                "parent_index": index_of[parent] if parent is not None else None,
                "children": list(children[index]),
                "transform": {"translation": translation},
            }
        )

    skins: list[dict[str, Any]] = []
    meshes: list[dict[str, Any]] = []
    skinning: list[dict[str, Any]] = []
    if entries and with_skinning:
        skin_joints = [
            {
                "node_index": index,
                "name": name,
                "parent_index": index_of[parent] if parent is not None else None,
                "parent_name": parent,
            }
            for index, (name, parent, _) in enumerate(entries)
        ]
        skins = [
            {
                "index": 0,
                "name": "Armature",
                "skeleton_node_index": 0,
                "inverse_bind_matrices_accessor": None,
                "joints": skin_joints,
            }
        ]
        meshes = [
            {
                "index": 0,
                "name": "Body",
                "primitive_count": 1,
                "primitives": [
                    {
                        "index": 0,
                        "mode": 4,
                        "material": None,
                        "attributes": {
                            "POSITION": {"index": 0, "count": 4, "component_type": 5126, "type": "VEC3"}
                        },
                        "indices": None,
                        "vertex_count": 4,
                        "skin_attributes": {
                            "JOINTS_0": {
                                "accessor": {"index": 1, "count": 4, "component_type": 5121, "type": "VEC4"}
                            },
                            "WEIGHTS_0": {
                                "accessor": {"index": 2, "count": 4, "component_type": 5126, "type": "VEC4"}
                            },
                        },
                    }
                ],
            }
        ]
        skinning = [
            {
                "mesh_index": 0,
                "primitive_index": 0,
                "node_index": 0,
                "skin_index": 0,
                "vertex_count": 4,
                "joints": [
                    {
                        "joint_index": index,
                        "node_index": index,
                        "name": name,
                        "influenced_vertex_count": 4,
                        "dominant_vertex_count": 4,
                        "weight_sum": 4.0,
                        "weighted_centroid": list(position),
                        "bounds_min": [position[0] - 0.05, position[1] - 0.05, position[2] - 0.05],
                        "bounds_max": [position[0] + 0.05, position[1] + 0.05, position[2] + 0.05],
                    }
                    for index, (name, _parent, position) in enumerate(entries)
                ],
            }
        ]

    return {
        "format": "glb",
        "version": 2,
        "source": source,
        "asset": {"generator": "synthetic", "version": "2.0"},
        "scene": 0,
        "scenes": [{"name": "Scene", "nodes": [0]}],
        "extensions_used": [],
        "extensions_required": [],
        "nodes": nodes,
        "skins": skins,
        "meshes": meshes,
        "skinning": skinning,
        "facts_only": True,
    }


def build_humanoid_facts() -> dict[str, Any]:
    return build_facts(HUMANOID_ENTRIES)


class CandidateGenerationContractTest(unittest.TestCase):
    """Top-level report contract shared by every fixture."""

    def setUp(self) -> None:
        self.facts = build_humanoid_facts()
        self.report = generate_body_candidates(self.facts)

    def test_report_is_json_serializable(self) -> None:
        round_trip = json.loads(json.dumps(self.report))
        self.assertEqual(round_trip, self.report)

    def test_report_required_keys_and_values(self) -> None:
        self.assertTrue(
            REQUIRED_REPORT_KEYS.issubset(self.report.keys()),
            f"missing report keys: {REQUIRED_REPORT_KEYS - self.report.keys()}",
        )
        self.assertEqual(self.report["format"], FORMAT)
        self.assertEqual(self.report["profile"], PROFILE)
        self.assertEqual(self.report["scope"], SCOPE)
        self.assertEqual(self.report["source"], self.facts["source"])
        self.assertEqual(self.report["asset"], self.facts["asset"])
        self.assertIsInstance(self.report["warnings"], list)

    def test_report_does_not_claim_final_mapping(self) -> None:
        self.assertEqual(set(self.report.keys()) & FINAL_MAPPING_KEYS, set())
        for slot in self.report["slots"]:
            for candidate in slot["candidates"]:
                self.assertEqual(set(candidate.keys()) & FINAL_FLAG_KEYS, set())

    def test_slots_exact_order_and_names(self) -> None:
        slots = self.report["slots"]
        self.assertEqual(len(slots), len(BODY_SLOTS))
        self.assertEqual([slot["profile_bone"] for slot in slots], BODY_SLOTS)

    def test_slots_shape_and_status(self) -> None:
        for slot in self.report["slots"]:
            self.assertTrue(REQUIRED_SLOT_KEYS.issubset(slot.keys()), slot.keys())
            self.assertIn(slot["status"], ALLOWED_STATUS, slot["profile_bone"])
            self.assertIsInstance(slot["candidates"], list)

    def test_candidate_shape_and_score_bounds(self) -> None:
        for slot in self.report["slots"]:
            for candidate in slot["candidates"]:
                self.assertTrue(
                    REQUIRED_CANDIDATE_KEYS.issubset(candidate.keys()), candidate.keys()
                )
                self.assertIsInstance(candidate["skeleton_bone"], str)
                score = candidate["score"]
                self.assertIsInstance(score, (int, float))
                self.assertFalse(isinstance(score, bool))
                self.assertGreaterEqual(score, 0.0)
                self.assertLessEqual(score, 1.0)
                self.assertTrue(candidate["features"])
                self.assertTrue(candidate["evidence"])

    def test_candidates_reference_existing_nodes(self) -> None:
        node_names = {node["name"] for node in self.facts["nodes"]}
        for slot in self.report["slots"]:
            for candidate in slot["candidates"]:
                self.assertIn(candidate["skeleton_bone"], node_names)

    def test_status_implies_candidate_counts(self) -> None:
        for slot in self.report["slots"]:
            if slot["status"] == "candidate":
                self.assertGreaterEqual(len(slot["candidates"]), 1, slot["profile_bone"])
            elif slot["status"] == "ambiguous":
                self.assertGreaterEqual(len(slot["candidates"]), 2, slot["profile_bone"])
            elif slot["status"] == "unknown":
                self.assertEqual(slot["candidates"], [], slot["profile_bone"])

    def test_candidates_sorted_by_score_desc(self) -> None:
        for slot in self.report["slots"]:
            scores = [candidate["score"] for candidate in slot["candidates"]]
            self.assertEqual(scores, sorted(scores, reverse=True), slot["profile_bone"])

    def test_deterministic_output_and_no_input_mutation(self) -> None:
        facts_copy = copy.deepcopy(self.facts)
        first = generate_body_candidates(self.facts)
        second = generate_body_candidates(self.facts)
        self.assertEqual(first, second)
        self.assertEqual(self.facts, facts_copy, "generate_body_candidates mutated its input")


class HumanoidCandidatesTest(unittest.TestCase):
    """Named, symmetric, skinned skeleton: every slot resolves to its own bone."""

    def setUp(self) -> None:
        self.facts = build_humanoid_facts()
        self.report = generate_body_candidates(self.facts)
        self.slots_by_bone = {slot["profile_bone"]: slot for slot in self.report["slots"]}

    def test_every_slot_has_candidate_status(self) -> None:
        for profile_bone in BODY_SLOTS:
            slot = self.slots_by_bone[profile_bone]
            self.assertIn(slot["status"], {"candidate", "ambiguous"}, profile_bone)
            self.assertGreaterEqual(len(slot["candidates"]), 1, profile_bone)

    def test_exact_name_match_is_candidate(self) -> None:
        for profile_bone in BODY_SLOTS:
            slot = self.slots_by_bone[profile_bone]
            candidates_by_bone = {c["skeleton_bone"]: c for c in slot["candidates"]}
            self.assertIn(profile_bone, candidates_by_bone, profile_bone)
            self.assertGreater(candidates_by_bone[profile_bone]["score"], 0.0, profile_bone)

    def test_score_weights_exact_match_above_others(self) -> None:
        for profile_bone in BODY_SLOTS:
            slot = self.slots_by_bone[profile_bone]
            candidates_by_bone = {c["skeleton_bone"]: c for c in slot["candidates"]}
            own = candidates_by_bone[profile_bone]["score"]
            others = [c["score"] for c in slot["candidates"] if c["skeleton_bone"] != profile_bone]
            self.assertTrue(all(own >= other for other in others), profile_bone)

    def test_left_right_mirrors_do_not_swap(self) -> None:
        # For a symmetric skeleton each side resolves to its own bone; the
        # mirrored bone may appear as a low-score alternative but must never
        # outrank the exact match (covered per-slot above).
        pairs = [
            ("LeftUpperArm", "RightUpperArm"),
            ("LeftLowerArm", "RightLowerArm"),
            ("LeftHand", "RightHand"),
            ("LeftUpperLeg", "RightUpperLeg"),
            ("LeftLowerLeg", "RightLowerLeg"),
            ("LeftFoot", "RightFoot"),
        ]
        for left, right in pairs:
            left_names = {c["skeleton_bone"] for c in self.slots_by_bone[left]["candidates"]}
            right_names = {c["skeleton_bone"] for c in self.slots_by_bone[right]["candidates"]}
            self.assertIn(left, left_names)
            self.assertIn(right, right_names)


class AbsentEvidenceTest(unittest.TestCase):
    """No semantic evidence => unknown/empty, never an invented candidate."""

    def test_minimal_skeleton_no_invented_candidates(self) -> None:
        facts = build_facts(
            [("Root", None, (0.0, 0.0, 0.0))],
            source="synthetic://minimal.glb",
            with_skinning=False,
        )
        report = generate_body_candidates(facts)
        slots = {slot["profile_bone"]: slot for slot in report["slots"]}
        self.assertEqual(len(slots), len(BODY_SLOTS))
        for profile_bone in BODY_SLOTS[1:]:  # every body part except Root
            slot = slots[profile_bone]
            self.assertEqual(slot["status"], "unknown", profile_bone)
            self.assertEqual(slot["candidates"], [], profile_bone)
        self.assertIn(slots["Root"]["status"], ALLOWED_STATUS)

    def test_empty_skeleton_all_unknown(self) -> None:
        facts = build_facts([], source="synthetic://empty.glb", with_skinning=False)
        report = generate_body_candidates(facts)
        self.assertEqual(len(report["slots"]), len(BODY_SLOTS))
        for slot in report["slots"]:
            self.assertEqual(slot["status"], "unknown", slot["profile_bone"])
            self.assertEqual(slot["candidates"], [], slot["profile_bone"])

    def test_generic_names_never_claim_confident_mapping(self) -> None:
        # A chain of opaque names with body-like offsets: without semantic name
        # evidence no slot may claim a perfect (score == 1.0) mapping.
        entries = [
            ("Bone_001", None, (0.0, 0.0, 0.0)),
            ("Bone_002", "Bone_001", (0.0, 0.5, 0.0)),
            ("Bone_003", "Bone_002", (0.0, 1.0, 0.0)),
            ("Bone_004", "Bone_003", (0.0, 1.5, 0.0)),
        ]
        facts = build_facts(entries, source="synthetic://generic.glb", with_skinning=False)
        report = generate_body_candidates(facts)
        for slot in report["slots"]:
            self.assertIn(slot["status"], ALLOWED_STATUS, slot["profile_bone"])
            for candidate in slot["candidates"]:
                self.assertLess(candidate["score"], 1.0, slot["profile_bone"])
                self.assertTrue(candidate["evidence"], slot["profile_bone"])


if __name__ == "__main__":
    unittest.main()
