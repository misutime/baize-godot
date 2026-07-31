"""Static metadata for Godot SkeletonProfileHumanoid."""
from __future__ import annotations
from typing import Any

_BONES = (
    ("Root", None, "Body"), ("Hips", "Root", "Body"), ("Spine", "Hips", "Body"),
    ("Chest", "Spine", "Body"), ("UpperChest", "Chest", "Body"), ("Neck", "UpperChest", "Body"),
    ("Head", "Neck", "Body"), ("LeftEye", "Head", "Face"), ("RightEye", "Head", "Face"),
    ("Jaw", "Head", "Face"), ("LeftShoulder", "UpperChest", "Body"),
    ("LeftUpperArm", "LeftShoulder", "Body"), ("LeftLowerArm", "LeftUpperArm", "Body"),
    ("LeftHand", "LeftLowerArm", "Body"), ("LeftThumbMetacarpal", "LeftHand", "LeftHand"),
    ("LeftThumbProximal", "LeftThumbMetacarpal", "LeftHand"), ("LeftThumbDistal", "LeftThumbProximal", "LeftHand"),
    ("LeftIndexProximal", "LeftHand", "LeftHand"), ("LeftIndexIntermediate", "LeftIndexProximal", "LeftHand"),
    ("LeftIndexDistal", "LeftIndexIntermediate", "LeftHand"), ("LeftMiddleProximal", "LeftHand", "LeftHand"),
    ("LeftMiddleIntermediate", "LeftMiddleProximal", "LeftHand"), ("LeftMiddleDistal", "LeftMiddleIntermediate", "LeftHand"),
    ("LeftRingProximal", "LeftHand", "LeftHand"), ("LeftRingIntermediate", "LeftRingProximal", "LeftHand"),
    ("LeftRingDistal", "LeftRingIntermediate", "LeftHand"), ("LeftLittleProximal", "LeftHand", "LeftHand"),
    ("LeftLittleIntermediate", "LeftLittleProximal", "LeftHand"), ("LeftLittleDistal", "LeftLittleIntermediate", "LeftHand"),
    ("RightShoulder", "UpperChest", "Body"), ("RightUpperArm", "RightShoulder", "Body"),
    ("RightLowerArm", "RightUpperArm", "Body"), ("RightHand", "RightLowerArm", "Body"),
    ("RightThumbMetacarpal", "RightHand", "RightHand"), ("RightThumbProximal", "RightThumbMetacarpal", "RightHand"),
    ("RightThumbDistal", "RightThumbProximal", "RightHand"), ("RightIndexProximal", "RightHand", "RightHand"),
    ("RightIndexIntermediate", "RightIndexProximal", "RightHand"), ("RightIndexDistal", "RightIndexIntermediate", "RightHand"),
    ("RightMiddleProximal", "RightHand", "RightHand"), ("RightMiddleIntermediate", "RightMiddleProximal", "RightHand"),
    ("RightMiddleDistal", "RightMiddleIntermediate", "RightHand"), ("RightRingProximal", "RightHand", "RightHand"),
    ("RightRingIntermediate", "RightRingProximal", "RightHand"), ("RightRingDistal", "RightRingIntermediate", "RightHand"),
    ("RightLittleProximal", "RightHand", "RightHand"), ("RightLittleIntermediate", "RightLittleProximal", "RightHand"),
    ("RightLittleDistal", "RightLittleIntermediate", "RightHand"), ("LeftUpperLeg", "Hips", "Body"),
    ("LeftLowerLeg", "LeftUpperLeg", "Body"), ("LeftFoot", "LeftLowerLeg", "Body"), ("LeftToes", "LeftFoot", "Body"),
    ("RightUpperLeg", "Hips", "Body"), ("RightLowerLeg", "RightUpperLeg", "Body"),
    ("RightFoot", "RightLowerLeg", "Body"), ("RightToes", "RightFoot", "Body"),
)
_REQUIRED = {"Hips", "Spine", "Head", "LeftShoulder", "LeftUpperArm", "LeftLowerArm", "LeftHand", "RightShoulder", "RightUpperArm", "RightLowerArm", "RightHand", "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "RightUpperLeg", "RightLowerLeg", "RightFoot"}


def profile_reference() -> dict[str, Any]:
    return {
        "name": "SkeletonProfileHumanoid",
        "bone_count": len(_BONES),
        "root_bone": "Root",
        "scale_base_bone": "Hips",
        "bones": [{"index": i, "name": n, "parent": p, "group": g, "required": n in _REQUIRED} for i, (n, p, g) in enumerate(_BONES)],
    }
