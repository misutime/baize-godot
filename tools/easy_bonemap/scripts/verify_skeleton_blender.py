import bpy
import json
import sys
from mathutils import Vector
from pathlib import Path


def args_after_separator():
    values = sys.argv[sys.argv.index("--") + 1:]
    if len(values) != 3:
        raise RuntimeError("Usage: blender --background --python script.py -- model.glb graph.json image.png")
    return values


def longest_path(roots, children, edges):
    def visit(name):
        if not children[name]:
            return 0.0
        return max(edges[(name, child)] + visit(child) for child in children[name])
    return max((visit(root) for root in roots), default=0.0)


def gltf_to_blender(value):
    return Vector((value[0], -value[2], value[1]))


def material(name, color):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1.0)
    return mat


def segments_object(name, segments, mat, bevel):
    curve = bpy.data.curves.new(name, type="CURVE")
    curve.dimensions = "3D"
    curve.bevel_depth = bevel
    curve.bevel_resolution = 2
    for start, end in segments:
        spline = curve.splines.new("POLY")
        spline.points.add(1)
        spline.points[0].co = (*start, 1.0)
        spline.points[1].co = (*end, 1.0)
    obj = bpy.data.objects.new(name, curve)
    bpy.context.collection.objects.link(obj)
    curve.materials.append(mat)
    return obj


def shifted_segments(segments, shift):
    offset = Vector(shift)
    return [(start + offset, end + offset) for start, end in segments]


def add_label(text, location, mat):
    bpy.ops.object.text_add(location=location)
    obj = bpy.context.object
    obj.data.body = text
    obj.data.align_x = "CENTER"
    obj.data.size = 0.09
    obj.data.extrude = 0.001
    obj.data.materials.append(mat)
    return obj


def main():
    glb_path, graph_path, image_path = args_after_separator()
    graph = json.loads(Path(graph_path).read_text())
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=glb_path)
    armature = next((obj for obj in bpy.context.scene.objects if obj.type == "ARMATURE"), None)
    if armature is None:
        raise RuntimeError("Blender imported no armature")
    actual = {bone.name: bone for bone in armature.data.bones}
    actual_positions = {name: armature.matrix_world @ bone.head_local for name, bone in actual.items()}
    actual_parent = {name: bone.parent.name if bone.parent and bone.parent.name in actual else None for name, bone in actual.items()}
    actual_children = {name: [] for name in actual}
    for name, parent in actual_parent.items():
        if parent is not None:
            actual_children[parent].append(name)
    actual_roots = [name for name, parent in actual_parent.items() if parent is None]
    actual_edges = {(parent, child): (actual_positions[child] - actual_positions[parent]).length for child, parent in actual_parent.items() if parent is not None}
    actual_scale = longest_path(actual_roots, actual_children, actual_edges)
    actual_origin = sum((actual_positions[root] for root in actual_roots), Vector((0, 0, 0))) / max(len(actual_roots), 1)
    actual_norm = {name: (position - actual_origin) / actual_scale for name, position in actual_positions.items()} if actual_scale > 1e-8 else {}

    output_bones = graph["bones"]
    output_by_name = {bone["name"]: bone for bone in output_bones}
    shared = sorted(set(actual_norm) & set(output_by_name))
    errors = [(name, (actual_norm[name] - gltf_to_blender(output_by_name[name]["normalized_position"])).length) for name in shared]
    max_error = max((error for _, error in errors), default=0.0)
    missing = sorted(set(output_by_name) - set(actual_norm))
    extra = sorted(set(actual_norm) - set(output_by_name))
    print(json.dumps({
        "armature": armature.name,
        "coordinate_conversion": "GLTF[x,y,z] -> Blender[x,-z,y]",
        "blender_bones": len(actual),
        "graph_bones": len(output_bones),
        "shared_bones": len(shared),
        "max_normalized_head_error": max_error,
        "missing_in_blender": missing,
        "extra_in_blender": extra,
        "blender_roots": actual_roots,
        "graph_roots": graph["roots"],
    }, indent=2))

    # Hide imported mesh for a clean full-skeleton comparison. Blender has already
    # independently parsed the GLB and supplied the armature data above.
    for obj in list(bpy.context.scene.objects):
        obj.hide_render = True

    blue = material("BlenderImported", (0.02, 0.25, 1.0))
    orange = material("GraphOutput", (1.0, 0.08, 0.01))
    white = material("Labels", (0.9, 0.9, 0.9))
    blender_segments = [(actual_norm[parent], actual_norm[child]) for parent, child in actual_edges if parent in actual_norm and child in actual_norm]
    output_by_index = {bone["index"]: bone for bone in output_bones}
    graph_segments = []
    for bone in output_bones:
        if bone["parent"] != -1:
            parent = output_by_index[bone["parent"]]
            graph_segments.append((gltf_to_blender(parent["normalized_position"]), gltf_to_blender(bone["normalized_position"])))

    all_points = list(actual_norm.values()) + [gltf_to_blender(b["normalized_position"]) for b in output_bones if b["normalized_position"] is not None]
    base_min = Vector((min(p.x for p in all_points), min(p.y for p in all_points), min(p.z for p in all_points)))
    base_max = Vector((max(p.x for p in all_points), max(p.y for p in all_points), max(p.z for p in all_points)))
    span = base_max - base_min
    offset = max(span.x, span.y, span.z) * 1.35
    actual_shift = Vector((-offset, 0, 0))
    graph_shift = Vector((0, 0, 0))
    overlay_shift = Vector((offset, 0, 0))
    segments_object("ActualBlender", shifted_segments(blender_segments, actual_shift), blue, 0.004)
    segments_object("GraphOnly", shifted_segments(graph_segments, graph_shift), orange, 0.004)
    segments_object("OverlayActual", shifted_segments(blender_segments, overlay_shift), blue, 0.0025)
    # A tiny depth offset only in the overlay panel makes coincident layers visible.
    overlay_graph = [(start + overlay_shift + Vector((0, 0, 0.004)), end + overlay_shift + Vector((0, 0, 0.004))) for start, end in graph_segments]
    segments_object("OverlayGraph", overlay_graph, orange, 0.006)
    label_y = base_min.y - span.y * 0.12
    add_label("Blender imported", actual_shift + Vector((span.x / 2, label_y, 0)), white)
    add_label("Normalized Graph", graph_shift + Vector((span.x / 2, label_y, 0)), white)
    add_label("Overlay (orange depth offset)", overlay_shift + Vector((span.x / 2, label_y, 0)), white)

    points_for_camera = all_points + [p + actual_shift for p in all_points] + [p + overlay_shift for p in all_points]
    center = sum(points_for_camera, Vector((0, 0, 0))) / len(points_for_camera)
    radius = max((point - center).length for point in points_for_camera)
    bpy.ops.object.camera_add(location=center + Vector((radius * 2.7, -radius * 2.7, radius * 1.8)))
    camera = bpy.context.object
    camera.data.lens = 58
    camera.rotation_euler = (center - camera.location).to_track_quat("-Z", "Y").to_euler()
    bpy.context.scene.camera = camera
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "MATERIAL"
    scene.display.shading.show_shadows = False
    scene.display.shading.show_cavity = True
    scene.render.resolution_x = 1500
    scene.render.resolution_y = 900
    scene.render.resolution_percentage = 100
    scene.render.filepath = image_path
    scene.render.image_settings.file_format = "PNG"
    if scene.world is None:
        scene.world = bpy.data.worlds.new("ValidationWorld")
    scene.world.color = (0.015, 0.015, 0.015)
    bpy.ops.render.render(write_still=True)
    print("rendered", image_path)


main()
