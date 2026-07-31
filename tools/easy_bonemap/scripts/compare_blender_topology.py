import bpy
import json
import sys
from pathlib import Path


def main():
    values = sys.argv[sys.argv.index("--") + 1:]
    if len(values) != 2:
        raise RuntimeError("Usage: blender --background --python script.py -- model.glb graph.json")
    glb_path, graph_path = values
    graph = json.loads(Path(graph_path).read_text())
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=glb_path)
    armature = next(obj for obj in bpy.context.scene.objects if obj.type == "ARMATURE")
    actual = {bone.name: bone for bone in armature.data.bones}
    graph_by_index = {bone["index"]: bone for bone in graph["bones"]}
    graph_by_name = {bone["name"]: bone for bone in graph["bones"]}
    graph_parent_name = {
        bone["name"]: graph_by_index[bone["parent"]]["name"] if bone["parent"] != -1 else None
        for bone in graph["bones"]
    }
    actual_parent_name = {
        name: bone.parent.name if bone.parent and bone.parent.name in actual else None
        for name, bone in actual.items()
    }
    shared = set(actual) & set(graph_by_name)
    mismatches = [
        (name, actual_parent_name[name], graph_parent_name[name])
        for name in sorted(shared)
        if actual_parent_name[name] != graph_parent_name[name]
    ]
    print(json.dumps({
        "blender_bones": len(actual),
        "graph_bones": len(graph_by_index),
        "shared": len(shared),
        "parent_mismatches": len(mismatches),
        "first_mismatches": mismatches[:20],
        "blender_roots": sorted(name for name, parent in actual_parent_name.items() if parent is None),
        "graph_roots": sorted(bone["name"] for bone in graph["bones"] if bone["parent"] == -1),
    }, indent=2))


main()
