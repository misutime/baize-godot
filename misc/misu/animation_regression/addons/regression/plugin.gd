@tool
extends EditorPlugin
## Background acceptance run for the skeletal animation tools.
## Each scenario is one user-visible workflow. Results go to user-provided paths.

var results: Array[Dictionary] = []
var current := ""
var failures: PackedStringArray = []
var out_dir := ""
var fixtures := ""


func _enter_tree() -> void:
	# A scenario that stops on a script error must still end the process with a result file.
	get_tree().create_timer(240.0).timeout.connect(abort)
	run.call_deferred()


func abort() -> void:
	check(false, "the run stopped inside this scenario; see the editor log")
	write_results()
	get_tree().quit(1)


func write_results() -> void:
	finish()
	var failed := results.filter(func(entry: Dictionary) -> bool: return not entry["passed"])
	var summary := {
		"engine": Engine.get_version_info()["string"],
		"engine_hash": Engine.get_version_info()["hash"],
		"scenarios": results.size(),
		"failed": failed.size(),
		"results": results,
	}
	var file := FileAccess.open(out_dir.path_join("results.json"), FileAccess.WRITE)
	file.store_string(JSON.stringify(summary, "  "))
	file.close()
	print("ANIMATION_REGRESSION scenarios=%d failed=%d" % [results.size(), failed.size()])


func check(condition: bool, what: String) -> void:
	if not condition:
		failures.push_back(what)


func begin(scenario: String) -> void:
	finish()
	current = scenario
	failures = []


func finish() -> void:
	if current != "":
		results.push_back({"scenario": current, "passed": failures.is_empty(), "failures": failures})
		print(("PASS " if failures.is_empty() else "FAIL ") + current + ("" if failures.is_empty() else " :: " + "; ".join(failures)))
	current = ""


func settle(frames := 12) -> void:
	for i in frames:
		await get_tree().process_frame


func is_text_asset(path: String) -> bool:
	return FileAccess.get_file_as_string(path).begins_with("[gd_")


func cold(path: String) -> Resource:
	return ResourceLoader.load(path, "", ResourceLoader.CACHE_MODE_IGNORE)


func build_rig(bone_names: PackedStringArray, with_mesh: bool, clips: Dictionary, with_prop := false, clip_intervals := {}) -> Node3D:
	var root := Node3D.new()
	root.name = "Rig"
	var skeleton := Skeleton3D.new()
	skeleton.name = "Skeleton3D"
	root.add_child(skeleton)
	skeleton.owner = root
	for i in bone_names.size():
		skeleton.add_bone(bone_names[i])
		skeleton.set_bone_parent(i, i - 1)
		skeleton.set_bone_rest(i, Transform3D(Basis(), Vector3(0, 0.5 if i > 0 else 0.0, 0)))
	skeleton.reset_bone_poses()
	if with_mesh:
		var tool := SurfaceTool.new()
		tool.begin(Mesh.PRIMITIVE_TRIANGLES)
		for vertex: Vector3 in [Vector3(-0.2, 0, 0), Vector3(0.2, 0, 0), Vector3(0, 1.0, 0)]:
			tool.set_normal(Vector3(0, 0, 1))
			tool.set_bones(PackedInt32Array([0, 1, 0, 0]))
			tool.set_weights(PackedFloat32Array([0.5, 0.5, 0, 0]))
			tool.add_vertex(vertex)
		var body := MeshInstance3D.new()
		body.name = "Body"
		body.mesh = tool.commit()
		skeleton.add_child(body)
		body.owner = root
		body.skin = skeleton.create_skin_from_rest_transforms()
		body.skeleton = NodePath("..")
	if with_prop:
		var attachment := BoneAttachment3D.new()
		attachment.name = "Hand"
		attachment.bone_name = bone_names[bone_names.size() - 1]
		skeleton.add_child(attachment)
		attachment.owner = root
		var prop := MeshInstance3D.new()
		prop.name = "Prop"
		prop.mesh = BoxMesh.new()
		attachment.add_child(prop)
		prop.owner = root
	if not clips.is_empty():
		var player := AnimationPlayer.new()
		player.name = "AnimationPlayer"
		root.add_child(player)
		player.owner = root
		var library := AnimationLibrary.new()
		for clip_name: String in clips:
			var clip := Animation.new()
			clip.length = clips[clip_name]
			var track := clip.add_track(Animation.TYPE_ROTATION_3D)
			clip.track_set_path(track, NodePath("Skeleton3D:" + bone_names[bone_names.size() - 1]))
			var intervals: int = clip_intervals.get(clip_name, 4)
			for step in intervals + 1:
				var fraction := float(step) / intervals
				clip.rotation_track_insert_key(track, clip.length * fraction, Quaternion(Vector3.RIGHT, 1.2 * fraction))
			library.add_animation(clip_name, clip)
		player.add_animation_library("", library)
	return root


func write_glb(root: Node3D, path: String) -> void:
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	check(document.append_from_scene(root, state) == OK, "append " + path)
	check(document.write_to_filesystem(state, path) == OK, "write " + path)
	root.free()


func reports(result: Dictionary) -> Array:
	return (result.get("metadata", {}) as Dictionary).get("skeleton_validation", [])


func run() -> void:
	var arguments := OS.get_cmdline_user_args()
	out_dir = arguments[0] if arguments.size() > 0 else OS.get_user_data_dir()
	fixtures = out_dir.path_join("fixtures")
	DirAccess.make_dir_recursive_absolute(fixtures)
	while EditorInterface.get_resource_filesystem().is_scanning():
		await get_tree().process_frame
	await settle(30)
	var workbench := EditorInterface.get_animation_workbench()
	var bones := PackedStringArray(["Root", "Spine", "Arm"])

	begin("fixtures are generated without machine-specific assets")
	write_glb(build_rig(bones, true, {"Idle": 1.0}, true), fixtures.path_join("hero.glb"))
	write_glb(build_rig(bones, true, {}), fixtures.path_join("hero_alt.glb"))
	write_glb(build_rig(bones, true, {"Run": 2.0, "Jump": 0.5}), fixtures.path_join("moves.glb"))
	write_glb(build_rig(PackedStringArray(["Hips", "Tail"]), true, {"Wag": 1.0}), fixtures.path_join("other.glb"))

	begin("importing a model creates text skeleton, model and clip assets")
	var first := EditorInterface.import_animation_assets(fixtures.path_join("hero.glb"), "res://characters")
	check(first.get("error") == OK, str(first.get("message")))
	var report: Dictionary = reports(first)[0] if reports(first).size() > 0 else {}
	var skeleton_path: String = report.get("definition", "")
	var model_path: String = report.get("model", "")
	var first_clips: PackedStringArray = report.get("animations", PackedStringArray())
	check(skeleton_path.ends_with(".sk") and model_path.ends_with(".skmesh"), "asset paths " + str(report))
	check(first_clips.size() == 1 and first_clips[0].ends_with(".skanim"), "clip uses .skanim")
	for path: String in [skeleton_path, model_path] + Array(first_clips):
		check(path != "" and is_text_asset(path), "text asset " + path)
	var imported_model := (cold(model_path) as PackedScene).instantiate()
	var imported_player := imported_model.get_node_or_null("AnimationPlayer") as AnimationPlayer
	check(imported_player != null and imported_player.has_animation(&"Idle"), "model player reads the imported clip")
	check(imported_model.find_children("*", "AnimationTree", true, false).is_empty(), "the generated model has no animation tree")
	imported_model.free()

	begin("GLB clips detect their own sampling rate and accept a fixed override")
	var sampling_bones := PackedStringArray(["SamplingRoot", "SamplingTip"])
	var sampling_glb := fixtures.path_join("sampling.glb")
	write_glb(build_rig(sampling_bones, true, {"Rate30": 1.0, "Rate60": 1.0, "Sparse": 1.0}, false, {"Rate30": 30, "Rate60": 60, "Sparse": 1}), sampling_glb)
	for override_rate: int in [0, 48]:
		var document := GLTFDocument.new()
		var gltf_state := GLTFState.new()
		check(document.append_from_file(sampling_glb, gltf_state) == OK, "read sampling fixture")
		var baked_scene := document.generate_scene(gltf_state, override_rate, false, false)
		var baked_player := baked_scene.find_children("*", "AnimationPlayer", true, false)[0] as AnimationPlayer
		var sampling_result := EditorInterface.import_animation_assets(sampling_glb, "res://sampling/%d" % override_rate, {"fps": override_rate, "create_skeleton": true})
		check(sampling_result.get("error") == OK, str(sampling_result.get("message")))
		var sampling_reports := reports(sampling_result)
		var paths: PackedStringArray = sampling_reports[0].get("animations", PackedStringArray()) if not sampling_reports.is_empty() else PackedStringArray()
		check(paths.size() == 3, "all sampling clips saved")
		for path: String in paths:
			var expected_rate := override_rate
			if expected_rate == 0:
				expected_rate = 60 if path.get_file() == "Rate60.skanim" else 30
			# The asset importer can remove redundant keys after baking.
			var baked_clip := baked_player.get_animation(path.get_file().get_basename())
			check(baked_clip.track_get_key_count(0) == expected_rate + 1, "baked keys at expected FPS for " + path)
			var clip := cold(path) as Animation
			check(clip != null, "saved clip loads: " + path)
			if clip:
				check(is_equal_approx(clip.get_meta("_animation_sample_rate", 0.0), expected_rate), "saved FPS for " + path)
				check(is_equal_approx(clip.step, 1.0 / expected_rate), "timeline step for " + path)
				check(is_equal_approx(clip.length, 1.0), "unchanged duration for " + path)
		baked_scene.free()

	begin("rigid parts attached to bones stay in the imported model")
	var hero := (cold(model_path) as PackedScene).instantiate() if model_path != "" else null
	check(hero != null and hero.get_node_or_null("Skeleton3D/Arm/Prop") is MeshInstance3D, "attachment kept")
	check(hero != null and hero.get_node_or_null("Meshes/Body") is MeshInstance3D, "skinned mesh kept")
	if hero:
		hero.free()

	begin("a second model finds its skeleton automatically")
	var second := EditorInterface.import_animation_assets(fixtures.path_join("hero_alt.glb"), "res://characters")
	var second_report: Dictionary = reports(second)[0] if reports(second).size() > 0 else {}
	var second_model: String = second_report.get("model", "")
	check(second.get("error") == OK and second_report.get("definition", "") == skeleton_path, "reused " + str(second_report.get("definition")))

	begin("animation files find their skeleton and reach every model")
	var batch: Dictionary = workbench.import_files(PackedStringArray([fixtures.path_join("moves.glb")]), "res://characters", {"animation_only": true})
	check(batch.get("error") == OK, str(batch.get("message")))
	var definition := load(skeleton_path) as SkeletonDefinition
	check(definition.get_animation_library().get_animation_list().size() == 3, "catalog has 3 clips")
	for path: String in [model_path, second_model]:
		var instance := (load(path) as PackedScene).instantiate()
		var instance_player := instance.get_node("AnimationPlayer") as AnimationPlayer
		check(instance_player.get_animation_list().size() == 3, path + " sees 3 clips")
		var libraries: Array = instance_player.get_animation_library_list()
		check(libraries.size() == 1 and String(libraries[0]).is_empty(), path + " has only the skeleton library")
		instance.free()
	check((cold(skeleton_path) as SkeletonDefinition).get_animation_library().get_animation_list().size() == 3, "catalog persisted")

	begin("clip files in the animation directory are the only catalog")
	var animation_directory := definition.get_animation_directory_path()
	check(animation_directory == skeleton_path.get_basename() + "_anims", "directory next to the skeleton: " + animation_directory)
	var skeleton_text := FileAccess.get_file_as_string(skeleton_path)
	check(not skeleton_text.contains(".skanim") and not skeleton_text.contains("AnimationLibrary"), "skeleton file lists no clips")
	check(skeleton_text.contains("animation_directory = "), "skeleton file stores its animation directory")
	var catalog_entries := definition.get_animation_catalog()
	check(catalog_entries.size() == 3, "catalog lists every clip")
	for entry: Dictionary in catalog_entries:
		var entry_path: String = entry["path"]
		check(entry_path.get_base_dir() == animation_directory and entry_path.get_file().get_basename() == String(entry["name"]), "file name is the animation name: " + entry_path)

	begin("the first skeleton preview plays automatically")
	EditorInterface.edit_resource(definition)
	await settle(20)
	var workspace := EditorInterface.get_base_control().find_child("AnimationAssetWorkspace", true, false)
	var preview_search := workspace.find_child("ClipSearch", true, false) as LineEdit
	var surface := workspace.find_children("*", "SubViewportContainer", true, false)[0] as SubViewportContainer
	var preview_camera := (surface.get_child(0) as SubViewport).get_camera_3d()
	var bounds := AABB()
	var meshes := 0
	for visual in preview_camera.get_parent().get_children():
		if visual.scene_file_path != model_path:
			continue
		for mesh: MeshInstance3D in visual.find_children("*", "MeshInstance3D", true, false):
			var box := mesh.global_transform * mesh.get_aabb()
			bounds = box if meshes == 0 else bounds.merge(box)
			meshes += 1
	var aim := preview_camera.global_position.direction_to(bounds.get_center())
	check(meshes > 0 and aim.dot(-preview_camera.global_basis.z) > 0.999, "initial orbit looks at the model center")
	preview_search.grab_focus()
	var wheel := InputEventMouseButton.new()
	wheel.button_index = MOUSE_BUTTON_WHEEL_UP
	wheel.pressed = true
	surface.gui_input.emit(wheel)
	check(surface.has_focus(), "navigation moves keyboard focus from clip search to the preview")
	var first_preview := workbench.get_state()
	check(first_preview["document"] == skeleton_path, "the skeleton workbench is active")
	check(first_preview["playing"], "playback starts without input")
	check(first_preview["time"] > 0.0, "the playback time advances")

	begin("renaming a clip in the workbench renames its file and its animation")
	var jump_uid := ResourceLoader.get_resource_uid(animation_directory.path_join("Jump.skanim"))
	check(workbench.rename_animation("Jump", "Leap") == OK, workbench.get_last_error())
	await settle(4)
	check(definition.get_animation_library().has_animation("Leap") and not definition.get_animation_library().has_animation("Jump"), "library follows the file name")
	check(FileAccess.file_exists(animation_directory.path_join("Leap.skanim")) and not FileAccess.file_exists(animation_directory.path_join("Jump.skanim")), "file renamed")
	check(ResourceLoader.get_resource_uid(animation_directory.path_join("Leap.skanim")) == jump_uid, "clip keeps its UID")
	check(workbench.rename_animation("Leap", "Run") != OK, "an existing name is rejected")
	check(workbench.rename_animation("Leap", "Bad,Name") != OK, "an invalid name is rejected")
	var renamed_probe := (load(model_path) as PackedScene).instantiate()
	check((renamed_probe.get_node("AnimationPlayer") as AnimationPlayer).has_animation("Leap"), "models see the new name")
	renamed_probe.free()
	check(workbench.rename_animation("Leap", "Jump") == OK, workbench.get_last_error())

	begin("F2 renames the selected clip in place without a dialog")
	var inline_list := find_clip_list()
	var inline_item := find_clip_item(inline_list, &"Jump")
	check(inline_item != null, "Jump row exists")
	if inline_item:
		inline_item.select(0)
		await settle(8)
		var rename_key := InputEventKey.new()
		rename_key.keycode = KEY_F2
		rename_key.pressed = true
		inline_list.gui_input.emit(rename_key)
		await settle(4)
		var inline_editor: LineEdit = null
		for candidate: Node in inline_list.find_children("*", "LineEdit", true, false):
			if (candidate as LineEdit).is_visible_in_tree():
				inline_editor = candidate
		check(inline_editor != null and inline_editor.text == "Jump", "the row opens an in-place editor holding the bare name")
		if inline_editor:
			inline_editor.text = "Hop"
			inline_editor.text_submitted.emit("Hop")
			await settle(8)
			check(definition.get_animation_library().has_animation("Hop") and not definition.get_animation_library().has_animation("Jump"), "in-place edit renamed the animation")
			check(FileAccess.file_exists(animation_directory.path_join("Hop.skanim")), "in-place edit renamed the file")
			check(workbench.get_state()["animation"] == "Hop", "renamed clip stays selected: " + str(workbench.get_state()["animation"]))
			check(workbench.rename_animation("Hop", "Jump") == OK, workbench.get_last_error())

	begin("a folder below the animation directory is an animation library")
	check(workbench.create_animation_library("Combat") == OK, workbench.get_last_error())
	check(definition.get_animation_library_names().has(&"Combat"), "empty folder is a library")
	check(workbench.move_animation("Jump", "Combat") == OK, workbench.get_last_error())
	check(FileAccess.file_exists(animation_directory.path_join("Combat/Jump.skanim")), "clip moved into the folder")
	var skeleton_list := find_clip_list()
	var skeleton_icon := skeleton_list.get_theme_icon("SKSkeleton", "EditorIcons")
	var skeleton_default := find_clip_item(skeleton_list, &"Idle")
	var skeleton_named := find_clip_item(skeleton_list, &"Combat/Jump")
	check(skeleton_icon != null and skeleton_default != null and skeleton_default.get_parent().get_icon(0) == skeleton_icon, "default skeleton library shows the skeleton icon")
	check(skeleton_icon != null and skeleton_named != null and skeleton_named.get_parent().get_icon(0) == skeleton_icon, "named skeleton library shows the skeleton icon")
	var library_probe := (load(model_path) as PackedScene).instantiate()
	var library_player := library_probe.get_node("AnimationPlayer") as AnimationPlayer
	check(library_player.has_animation("Combat/Jump") and not library_player.has_animation("Jump"), "plays as library/animation")
	library_probe.free()
	check(workbench.rename_animation_library("Combat", "Melee") == OK, workbench.get_last_error())
	check(definition.get_animation_library("Melee") != null and definition.get_animation_library("Melee").has_animation("Jump"), "folder rename renames the library")
	check(workbench.move_animation("Melee/Jump", "") == OK, workbench.get_last_error())
	DirAccess.remove_absolute(ProjectSettings.globalize_path(animation_directory.path_join("Melee")))
	definition.rescan_animations()
	check(definition.get_animation_library_names().size() == 1, "removing the folder removes the library")
	check(definition.get_animation_library().get_animation_list().size() == 3, "default library has every clip again")

	begin("the animation directory can be pointed at another folder")
	var other_directory := "res://characters/other_clips"
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(other_directory))
	check(workbench.set_animation_directory("res://characters/does_not_exist") != OK, "a missing folder is rejected")
	check(workbench.set_animation_directory(other_directory) == OK, workbench.get_last_error())
	check(definition.animation_directory == "other_clips", "stored relative to the skeleton: " + definition.animation_directory)
	check(definition.get_animation_library().get_animation_list().is_empty(), "the new folder has no clips")
	check((cold(skeleton_path) as SkeletonDefinition).animation_directory == "other_clips", "directory persisted")
	check(workbench.set_animation_directory(animation_directory) == OK, workbench.get_last_error())
	check(definition.get_animation_library().get_animation_list().size() == 3, "switching back restores the clips")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(other_directory))

	begin("clip files changed outside the editor are picked up by a rescan")
	var outside_from := ProjectSettings.globalize_path(animation_directory.path_join("Run.skanim"))
	var outside_to := ProjectSettings.globalize_path(animation_directory.path_join("Sprint.skanim"))
	DirAccess.rename_absolute(outside_from, outside_to)
	EditorInterface.get_resource_filesystem().scan()
	await settle(2)
	while EditorInterface.get_resource_filesystem().is_scanning():
		await get_tree().process_frame
	await settle(10)
	check(definition.get_animation_library().has_animation("Sprint") and not definition.get_animation_library().has_animation("Run"), "external rename detected")
	DirAccess.rename_absolute(outside_to, outside_from)
	EditorInterface.get_resource_filesystem().scan()
	await settle(2)
	while EditorInterface.get_resource_filesystem().is_scanning():
		await get_tree().process_frame
	await settle(10)
	check(definition.get_animation_library().has_animation("Run") and not definition.get_animation_library().has_animation("Sprint"), "external rename back detected")

	begin("files that cannot be animations are reported instead of loaded")
	var bad_file := ProjectSettings.globalize_path(animation_directory.path_join("Bad,Name.skanim"))
	DirAccess.copy_absolute(ProjectSettings.globalize_path(animation_directory.path_join("Idle.skanim")), bad_file)
	definition.rescan_animations()
	var issues := definition.get_animation_scan_issues()
	check(issues.size() == 1 and issues[0]["reason"] == "invalid_name", "invalid file name reported: " + str(issues))
	check(definition.get_animation_library().get_animation_list().size() == 3, "invalid file is not loaded")
	DirAccess.remove_absolute(bad_file)
	definition.rescan_animations()
	check(definition.get_animation_scan_issues().is_empty(), "issue clears with the file")

	begin("an exported skeleton carries its clips as stored libraries")
	var baked := cold(skeleton_path) as SkeletonDefinition
	baked.set_baked_animation_libraries(definition.bake_animation_libraries())
	check(ResourceSaver.save(baked, "res://baked_probe.res") == OK, "baked skeleton saves as binary")
	var baked_loaded := cold("res://baked_probe.res") as SkeletonDefinition
	check(baked_loaded != null and baked_loaded.has_baked_animation_libraries(), "stored libraries load")
	check(baked_loaded != null and baked_loaded.get_animation_library().get_animation_list().size() == 3, "stored library has every clip")
	check(not definition.has_baked_animation_libraries(), "the project skeleton keeps scanning its directory")

	begin("a skeleton-bound player rejects additional animation libraries")
	var bound_probe := (load(model_path) as PackedScene).instantiate()
	var bound_player := bound_probe.get_node("AnimationPlayer") as AnimationPlayer
	var local_library := AnimationLibrary.new()
	var local_clip := Animation.new()
	local_clip.length = 1.0
	local_library.add_animation("Local", local_clip)
	get_tree().root.add_child(bound_probe)
	await settle(1)
	check(bound_player.add_animation_library("Local", local_library) != OK, "additional library rejected while skeleton-bound")
	check(not bound_player.has_animation_library("Local"), "additional library not installed")
	bound_player.skeleton_definition = null
	check(bound_player.add_animation_library("Local", local_library) == OK, "local library allowed after unbinding")
	bound_probe.free()

	begin("legacy additional libraries are removed when a skeleton-bound player enters the tree")
	var legacy_probe := (load(model_path) as PackedScene).instantiate()
	var legacy_player := legacy_probe.get_node("AnimationPlayer") as AnimationPlayer
	check(legacy_player.add_animation_library("Legacy", local_library) == OK, "legacy library loads before entering the tree")
	get_tree().root.add_child(legacy_probe)
	await settle(1)
	check(not legacy_player.has_animation_library("Legacy"), "legacy library removed on enter tree")
	check(legacy_player.has_animation_library(""), "skeleton library remains after cleanup")
	legacy_probe.free()

	begin("preview model candidates are project SKMeshes using the skeleton")
	EditorInterface.edit_resource(definition)
	await settle(20)
	var preview_state := workbench.get_state()
	check(preview_state.get("preview_model") == model_path, "stored preview " + str(preview_state.get("preview_model")))
	var preview_candidates: PackedStringArray = preview_state.get("preview_model_candidates", PackedStringArray())
	check(preview_candidates.has(model_path) and preview_candidates.has(second_model), "matching models listed: " + str(preview_candidates))
	for candidate: String in preview_candidates:
		check(candidate.ends_with(".skmesh"), "candidate is SKMesh " + candidate)

	begin("one close-button click closes the active skeleton document")
	var skeleton_tabs := find_scene_tabs()
	var skeleton_tab := find_document_tab(skeleton_tabs, skeleton_path)
	check(skeleton_tab >= 0, "skeleton document tab exists")
	check(EditorInterface.get_active_editor_document() == skeleton_path, "skeleton document is active")
	if skeleton_tabs and skeleton_tab >= 0:
		await click_tab_close(skeleton_tabs, skeleton_tab)
	await settle(20)
	check(EditorInterface.get_active_editor_document().is_empty(), "one close click returns to the scene")
	check(find_document_tab(skeleton_tabs, skeleton_path) < 0, "skeleton tab is removed on the first click")

	begin("a file for another skeleton is rejected with the reason")
	var wrong := EditorInterface.import_animation_assets(fixtures.path_join("other.glb"), "res://characters", {"skeleton": skeleton_path})
	check(wrong.get("error") != OK, "rejected")
	check(str(wrong.get("message")).contains("Hips") or str(wrong.get("message")).contains("missing_bones"), "reason names bones: " + str(wrong.get("message")))

	begin("native binary .anim resources still load")
	var native_clip := Animation.new()
	native_clip.length = 2.0
	check(ResourceSaver.save(native_clip, "res://native.anim") == OK, "saved native .anim")
	check(not is_text_asset("res://native.anim"), "native .anim is binary")
	check(cold("res://native.anim") is Animation, "native .anim loads")

	begin("removing the skeleton from a player removes its shared catalog")
	var probe := (load(model_path) as PackedScene).instantiate()
	var probe_player := probe.get_node("AnimationPlayer") as AnimationPlayer
	probe_player.skeleton_definition = null
	check(not probe_player.has_animation_library(""), "derived library removed")
	probe.free()

	begin("a copied skeleton file can be saved")
	DirAccess.copy_absolute(ProjectSettings.globalize_path(skeleton_path), ProjectSettings.globalize_path("res://characters/copy.sk"))
	# The FileSystem dock gives a duplicated file its own identity; a raw copy needs it assigned.
	ResourceSaver.set_uid("res://characters/copy.sk", ResourceUID.create_id())
	var copy := cold("res://characters/copy.sk") as SkeletonDefinition
	check(copy != null and ResourceSaver.save(copy, "res://characters/copy.sk") == OK, "copy saves")
	check(copy != null and copy.get_animation_library().get_animation_list().is_empty(), "copy owns no clips")
	check((load(skeleton_path) as SkeletonDefinition).get_animation_library().get_animation_list().size() == 3, "original keeps clips")

	begin("a skeleton that still lists its clips moves them into its animation directory")
	var legacy_clip := (load(first_clips[0]) as Animation).duplicate(true) as Animation
	legacy_clip.skeleton = cold("res://characters/copy.sk")
	check(ResourceSaver.save(legacy_clip, "res://characters/legacy_walk.skanim") == OK, "legacy clip prepared")
	var legacy_text := FileAccess.get_file_as_string("res://characters/copy.sk")
	var header_end := legacy_text.find("\n") + 1
	legacy_text = legacy_text.substr(0, header_end) + "\n[ext_resource type=\"Animation\" path=\"res://characters/legacy_walk.skanim\" id=\"1_clip\"]\n\n[sub_resource type=\"AnimationLibrary\" id=\"AnimationLibrary_shared\"]\n_data = {\n&\"Walk\": ExtResource(\"1_clip\")\n}\n" + legacy_text.substr(header_end)
	legacy_text = legacy_text.replace("[resource]\n", "[resource]\nanimation_library = SubResource(\"AnimationLibrary_shared\")\nanimation_metadata = {}\n")
	legacy_text = legacy_text.replace("animation_directory = \"" + animation_directory.get_file() + "\"\n", "")
	var legacy_file := FileAccess.open("res://characters/copy.sk", FileAccess.WRITE)
	legacy_file.store_string(legacy_text)
	legacy_file.close()
	var legacy := load("res://characters/copy.sk") as SkeletonDefinition
	check(legacy != null and legacy.has_baked_animation_libraries(), "old file loads with its stored list")
	check(legacy != null and legacy.get_animation_library().has_animation("Walk"), "stored clips stay usable before moving")
	var legacy_model := (cold(model_path) as PackedScene).instantiate()
	(legacy_model.get_node("AnimationPlayer") as AnimationPlayer).skeleton_definition = legacy
	var legacy_tree := AnimationTree.new()
	legacy_tree.name = "AnimationTree"
	legacy_tree.anim_player = NodePath("../AnimationPlayer")
	legacy_tree.active = false
	var legacy_graph := AnimationNodeAnimation.new()
	legacy_graph.animation = &"Walk"
	legacy_tree.tree_root = legacy_graph
	legacy_model.add_child(legacy_tree)
	legacy_tree.owner = legacy_model
	var legacy_scene := PackedScene.new()
	var legacy_model_path := "res://characters/legacy.skmesh"
	check(legacy_scene.pack(legacy_model) == OK and ResourceSaver.save(legacy_scene, legacy_model_path) == OK, "legacy model prepared")
	legacy_model.free()
	var legacy_update := EditorInterface.update_skeletal_mesh(fixtures.path_join("hero_alt.glb"), legacy_model_path)
	check(legacy_update.get("error") == OK, "legacy model updates")
	check(FileAccess.get_file_as_string("res://characters/copy.sk") == legacy_text, "mesh update leaves the legacy skeleton file intact")
	check(FileAccess.file_exists("res://characters/legacy_walk.skanim"), "mesh update leaves the legacy clip in place")
	var updated_legacy_model := (cold(legacy_model_path) as PackedScene).instantiate()
	var updated_tree := updated_legacy_model.get_node_or_null("AnimationTree") as AnimationTree
	check(updated_tree != null and updated_tree.anim_player == NodePath("../AnimationPlayer") and (updated_tree.tree_root as AnimationNodeAnimation).animation == &"Walk", "mesh update keeps the existing model's graph and player binding")
	updated_legacy_model.free()
	check(workbench.migrate_legacy_animations(legacy) == OK, workbench.get_last_error())
	check(not legacy.has_baked_animation_libraries() and legacy.get_animation_library().has_animation("Walk"), "clips come from the directory after moving")
	check(FileAccess.file_exists("res://characters/copy_anims/Walk.skanim") and not FileAccess.file_exists("res://characters/legacy_walk.skanim"), "clip file moved and named after the animation")
	var migrated_text := FileAccess.get_file_as_string("res://characters/copy.sk")
	check(not migrated_text.contains("AnimationLibrary") and migrated_text.contains("animation_directory = \"copy_anims\""), "skeleton file no longer lists clips")

	begin("range deletion works as a plain data operation")
	var data_clip := (load(first_clips[0]) as Animation).duplicate(true) as Animation
	data_clip.add_marker("late", 0.9)
	data_clip.add_marker("inside", 0.3)
	var cut: Dictionary = AnimationWorkbenchEditorPlugin.delete_clip_range(data_clip, 0.2, 0.5)
	var cut_clip := cut.get("animation") as Animation
	check(cut.get("error") == OK and cut_clip != null and is_equal_approx(cut_clip.length, 0.7), "length 0.7")
	check(cut_clip != null and is_equal_approx(cut_clip.get_marker_time("late"), 0.6), "marker shifted")
	check(cut_clip != null and not cut_clip.has_marker("inside"), "marker inside range removed")
	var invalid: Dictionary = AnimationWorkbenchEditorPlugin.delete_clip_range(data_clip, 0.5, 0.2)
	check(invalid.get("error") != OK and str(invalid.get("message")) != "", "invalid range explains itself")

	begin("a clip opens, is trimmed, undone, redone and saved to its own file")
	var clip_path := ""
	for name: StringName in definition.get_animation_library().get_animation_list():
		if String(name) == "Run":
			clip_path = definition.get_animation_library().get_animation(name).resource_path
	workbench.open_resource(load(clip_path))
	await settle(30)
	var state: Dictionary = workbench.get_state()
	check(state.get("document") == skeleton_path and state.get("animation") == "Run", "owning skeleton document and Run selected")
	check(state.get("has_preview_model"), "preview model available")
	check(is_equal_approx(state.get("length", 0.0), 2.0), "length 2.0")
	check(workbench.delete_range(0.5, 1.0) == OK, workbench.get_last_error())
	check(workbench.delete_range(0.0, 0.25) == OK, workbench.get_last_error())
	workbench.undo_edit()
	check(is_equal_approx(workbench.get_state()["length"], 1.5), "undo restores 1.5")
	workbench.redo_edit()
	check(is_equal_approx(workbench.get_state()["length"], 1.25) and workbench.get_state()["clip_modified"], "redo gives 1.25, modified")
	check(workbench.save_animation() == OK, workbench.get_last_error())
	check(not workbench.get_state()["clip_modified"], "clean after save")
	check(is_equal_approx((cold(clip_path) as Animation).length, 1.25), "file holds 1.25")

	begin("animation clips open the owning skeleton document")
	var scene_tabs := find_scene_tabs()
	check(scene_tabs != null, "central tab bar found")
	if scene_tabs:
		var tab_count := scene_tabs.tab_count
		var jump_clip := definition.get_animation_library().get_animation("Jump") as Animation
		var jump_path := jump_clip.get_path() if jump_clip else String()
		workbench.open_resource(jump_clip)
		await settle(20)
		check(scene_tabs.tab_count == tab_count, "opening Jump reuses the owning skeleton tab")
		check(EditorInterface.get_active_editor_document() == skeleton_path, "Jump activates its skeleton document")
		check(workbench.get_state()["document"] == skeleton_path and workbench.get_state()["animation"] == "Jump", "Jump is selected in the skeleton catalog")
		check(not (workbench.get_state()["open_documents"] as PackedStringArray).has(jump_path), "no standalone clip document was opened")
		workbench.open_resource(load(clip_path))
		await settle(20)
		check(scene_tabs.tab_count == tab_count, "opening Run reuses the owning skeleton tab")
		check(EditorInterface.get_active_editor_document() == skeleton_path, "Run activates its skeleton document")
		check(workbench.get_state()["document"] == skeleton_path and workbench.get_state()["animation"] == "Run", "Run is selected in the skeleton catalog")
		check(not (workbench.get_state()["open_documents"] as PackedStringArray).has(clip_path), "Run has no standalone clip document")

	begin("preview playback follows the workbench across clip files")
	var run_source := load(clip_path) as Animation
	var jump_source := definition.get_animation_library().get_animation("Jump") as Animation
	workbench.open_resource(run_source)
	await settle(20)
	workbench.set_playing(true)
	await settle(2)
	check(workbench.get_state()["playing"], "Run starts playing")
	workbench.open_resource(jump_source)
	await settle(20)
	check(workbench.get_state()["document"] == skeleton_path and workbench.get_state()["animation"] == "Jump", "Jump selected in the skeleton document")
	check(workbench.get_state()["playing"], "Jump opens with playback enabled")
	workbench.open_resource(run_source)
	await settle(20)
	check(workbench.get_state()["playing"], "Run reopens with playback enabled")
	workbench.set_playing(false)

	begin("unsaved clip markers are visible in the tab and clip list")
	var clip_list := find_clip_list()
	check(scene_tabs != null and clip_list != null, "tab bar and clip list found")
	if scene_tabs and clip_list:
		check(workbench.delete_range(0.0, 0.1) == OK, workbench.get_last_error())
		await settle(5)
		check(scene_tabs.get_tab_title(scene_tabs.current_tab).begins_with("* "), "tab title marks unsaved clip")
		var selected := clip_list.get_selected()
		check(selected != null and selected.get_text(0).begins_with("* "), "clip row marks unsaved clip")
		workbench.undo_edit()
		await settle(5)
		check(not scene_tabs.get_tab_title(scene_tabs.current_tab).begins_with("* "), "saved tab title clears marker")
		selected = clip_list.get_selected()
		check(selected != null and not selected.get_text(0).begins_with("* "), "saved clip row clears marker")

	begin("the clip loop mode is edited and saved in the workbench")
	check(workbench.set_loop_mode(Animation.LOOP_LINEAR) == OK, "set loop")
	check(workbench.get_state()["clip_modified"], "loop change marks modified")
	check(workbench.save_animation() == OK, workbench.get_last_error())
	check((cold(clip_path) as Animation).loop_mode == Animation.LOOP_LINEAR, "file holds loop mode")

	begin("preview transport settings stay global while clip loop mode follows the clip")
	workbench.set_preview_loop(false)
	workbench.set_preview_speed(2.0)
	workbench.set_playing(true)
	await settle(2)
	var other_name := StringName()
	for name: StringName in definition.get_animation_library().get_animation_list():
		if name != "Run":
			other_name = name
			break
	check(other_name != StringName(), "another clip exists")
	workbench.select_animation(other_name)
	await settle(2)
	state = workbench.get_state()
	var other_clip := definition.get_animation_library().get_animation(other_name) as Animation
	check(state.get("animation") == other_name, "clip switched")
	check(not state.get("preview_loop"), "preview loop stays off")
	check(is_equal_approx(state.get("preview_speed", 0.0), 2.0), "preview speed stays 2x")
	check(state.get("playing"), "playing state survives the clip switch")
	check(int(state.get("loop_mode", -1)) == int(other_clip.loop_mode), "clip loop mode follows the new clip")
	workbench.set_playing(false)
	workbench.select_animation("Run")
	await settle(2)

	begin("playback, seek and selection can be driven and read back")
	workbench.seek(0.5)
	workbench.set_selection(0.25, 0.75)
	workbench.set_playing(true)
	await settle(5)
	state = workbench.get_state()
	check(state["playing"], "playing")
	check(is_equal_approx(state["selection_start"], 0.25) and is_equal_approx(state["selection_end"], 0.75), "selection")
	workbench.set_playing(false)
	check(not workbench.get_state()["playing"], "paused")

	begin("switching clips keeps unsaved edits without a prompt")
	check(workbench.delete_range(0.0, 0.25) == OK, workbench.get_last_error())
	workbench.select_animation("Jump")
	await settle(5)
	check(workbench.get_state()["animation"] == &"Jump", "switched to Jump")
	workbench.select_animation("Run")
	await settle(5)
	check(workbench.get_state()["clip_modified"] and is_equal_approx(workbench.get_state()["length"], 1.0), "Run draft kept")
	workbench.undo_edit()

	begin("updating a model from a file keeps its skeleton and clips")
	var updated := EditorInterface.update_skeletal_mesh(fixtures.path_join("hero_alt.glb"), model_path)
	check(updated.get("error") == OK, str(updated.get("message")))
	var refreshed := (cold(model_path) as PackedScene).instantiate()
	var refreshed_player := refreshed.get_node("AnimationPlayer") as AnimationPlayer
	check(refreshed_player.skeleton_definition != null and refreshed_player.skeleton_definition.resource_path == skeleton_path, "skeleton kept")
	check(refreshed_player.get_animation_list().size() == 3, "clips kept")
	refreshed.free()

	begin("a bound player document tracks changes to its library groups")
	EditorInterface.open_scene_from_path(model_path)
	await settle(20)
	var model_player := EditorInterface.get_edited_scene_root().get_node("AnimationPlayer") as AnimationPlayer
	check(EditorInterface.open_animation_node(model_player), "bound player document opened")
	await settle(20)
	check(workbench.create_animation_library("Live") == OK, workbench.get_last_error())
	check(workbench.move_animation("Idle", "Live") == OK, workbench.get_last_error())
	await settle(12)
	check(model_player.has_animation("Live/Idle"), "scene player sees the move")
	check(find_clip_item(find_clip_list(), &"Live/Idle") != null, "open list sees the new library and clip")
	check(workbench.move_animation("Live/Idle", "") == OK, workbench.get_last_error())
	DirAccess.remove_absolute(animation_directory.path_join("Live"))
	definition.rescan_animations()
	await settle(12)
	check(find_clip_item(find_clip_list(), &"Idle") != null, "clip returns to the default library")
	if DirAccess.open(animation_directory).is_case_sensitive(animation_directory):
		var lower_path := animation_directory.path_join("idle.skanim")
		var lower_clip := (load(first_clips[0]) as Animation).duplicate() as Animation
		check(ResourceSaver.save(lower_clip, lower_path) == OK, "separate lowercase clip prepared")
		var lower_bytes := FileAccess.get_file_as_bytes(lower_path)
		definition.rescan_animations()
		check(workbench.rename_animation("Idle", "idle") == ERR_ALREADY_EXISTS, "case-sensitive collision is rejected")
		check(FileAccess.file_exists(first_clips[0]) and FileAccess.get_file_as_bytes(lower_path) == lower_bytes, "both clip files survive the conflict")
		DirAccess.remove_absolute(lower_path)
		definition.rescan_animations()

	begin("a plain 3D scene has built-in and external animation libraries")
	var scene_root := Node3D.new()
	scene_root.name = "Door"
	var plain := AnimationPlayer.new()
	plain.name = "AnimationPlayer"
	var plain_library := AnimationLibrary.new()
	var frame_clip := Animation.new()
	frame_clip.length = 2.99
	frame_clip.step = 1.0 / 30.0
	var position_track := frame_clip.add_track(Animation.TYPE_POSITION_3D)
	frame_clip.track_set_path(position_track, NodePath("Sample"))
	for frame: int in range(90):
		frame_clip.position_track_insert_key(position_track, frame / 30.0, Vector3(frame, 0, 0))
	frame_clip.position_track_insert_key(position_track, frame_clip.length, Vector3(90, 0, 0))
	for frame: int in [9, 10, 13, 14, 29, 30, 59, 60, 61, 89]:
		frame_clip.add_marker("frame_%d" % frame, frame / 30.0)
	frame_clip.add_marker("terminal", frame_clip.length)
	plain_library.add_animation(&"Open", frame_clip)
	plain.add_animation_library(&"", plain_library)
	var named_library := AnimationLibrary.new()
	var time_clip := Animation.new()
	time_clip.length = 3.0
	time_clip.step = 0.0
	var value_track := time_clip.add_track(Animation.TYPE_VALUE)
	time_clip.track_set_path(value_track, NodePath("Sample:position:x"))
	for key_time: float in [0.9, 1.0, 1.5, 2.0, 2.1]:
		time_clip.track_insert_key(value_track, key_time, key_time)
	time_clip.add_marker("first", 1.0)
	time_clip.add_marker("last", 2.0)
	time_clip.add_marker("after", 2.1)
	named_library.add_animation(&"Close", time_clip)
	var library_path := "res://door_library.tres"
	check(ResourceSaver.save(named_library, library_path, ResourceSaver.FLAG_CHANGE_PATH) == OK, "external library saved")
	plain.add_animation_library(&"Door", load(library_path))
	scene_root.add_child(plain)
	plain.owner = scene_root
	var sample := Node3D.new()
	sample.name = "Sample"
	scene_root.add_child(sample)
	sample.owner = scene_root
	var packed := PackedScene.new()
	packed.pack(scene_root)
	ResourceSaver.save(packed, "res://door.tscn")
	scene_root.free()
	EditorInterface.open_scene_from_path("res://door.tscn")
	await settle(30)

	begin("unsaved scene tab uses the shared leading marker")
	EditorInterface.mark_scene_as_unsaved()
	await settle(5)
	var tab_bar := find_scene_tabs()
	var door_tab := -1
	for index: int in tab_bar.tab_count:
		if tab_bar.get_tab_tooltip(index).ends_with("door.tscn"):
			door_tab = index
			break
	check(door_tab >= 0, "door tab found")
	check(door_tab >= 0 and tab_bar.get_tab_title(door_tab).begins_with("* "), "scene tab uses leading marker")
	check(door_tab >= 0 and not tab_bar.get_tab_title(door_tab).ends_with("(*)"), "scene tab has no suffix marker")
	check(EditorInterface.save_scene() == OK, "scene saved")

	begin("a plain 3D AnimationPlayer uses the native track editor")
	var opened_player := EditorInterface.get_edited_scene_root().get_node("AnimationPlayer") as AnimationPlayer
	EditorInterface.get_selection().clear()
	EditorInterface.get_selection().add_node(opened_player)
	await settle()
	var player_tree := EditorInterface.get_base_control().find_child("SceneTreeEditorTree", true, false) as Tree
	player_tree.item_activated.emit()
	await settle()
	var native_dock := EditorInterface.get_base_control().find_children("*", "AnimationPlayerEditor", true, false)[0] as Control
	check(native_dock.is_visible_in_tree() and EditorInterface.get_active_editor_document().is_empty(), "3D tracks open in the native dock without a central tab")

	check(EditorInterface.open_animation_node(opened_player) and EditorInterface.get_active_editor_document().is_empty(), "the editor API follows the same native route")

	begin("the explicit workbench API still edits scene-owned clips")
	workbench.open_player(opened_player)
	await settle(20)
	check(EditorInterface.get_active_editor_document().contains("::AnimationPlayer/"), "active document " + EditorInterface.get_active_editor_document())
	var workbench_state := EditorInterface.get_animation_workbench().get_state()
	check(workbench_state["has_preview_model"], "workbench preview is available")
	var player_list := find_clip_list()
	var player_icon := player_list.get_theme_icon("AnimationPlayer", "EditorIcons")
	var player_default := find_clip_item(player_list, &"Open")
	var player_named := find_clip_item(player_list, &"Door/Close")
	check(player_icon != null and player_default != null and player_default.get_parent().get_icon(0) == player_icon, "default player library shows the player icon")
	check(player_icon != null and player_named != null and player_named.get_parent().get_icon(0) == player_icon, "named player library shows the player icon")

	begin("frame and seconds selection delete the same inclusive sample range")
	workbench.select_animation(&"Open")
	await settle(8)
	var timeline := get_tree().root.find_child("AnimationClipTimeline", true, false)
	var controls := timeline.get_parent()
	var units := controls.find_child("TimelineUnits", true, false) as OptionButton
	var first_frame := controls.find_child("SelectionStart", true, false) as SpinBox
	var last_frame := controls.find_child("SelectionEnd", true, false) as SpinBox
	var selection_summary := controls.find_child("SelectionSummary", true, false) as Label
	var delete_frames := controls.find_child("DeleteRange", true, false) as Button
	var frame_readout := controls.find_child("FrameReadout", true, false) as Label
	var seconds_readout := controls.find_child("SecondsReadout", true, false) as Label
	var sample_step := workbench.get_current_animation().step
	workbench.seek(0.0)
	check(seconds_readout.text == "0.000 / 2.990 s", "seconds readout pads trailing zeroes to three decimal places")
	var frame_readout_width := frame_readout.get_combined_minimum_size().x
	workbench.seek(2.0)
	check(seconds_readout.text == "2.000 / 2.990 s" and frame_readout.text.begins_with("60 / 90 "), "whole seconds keep the same precision during preview")
	check(is_equal_approx(frame_readout.get_combined_minimum_size().x, frame_readout_width), "frame readout width is stable as its digit count changes")
	check(units.selected == 0, "the workbench defaults to frames")
	check(delete_frames.disabled, "the initial selection is empty")
	check(first_frame.get_line_edit().text.is_empty() and last_frame.get_line_edit().text.is_empty(), "unselected endpoints show placeholders")
	check(not selection_summary.visible, "no summary before selecting a range")
	check(last_frame.max_value == 89, "the final partial frame is selectable")
	first_frame.value = 10
	last_frame.value = 13
	var summary_in_frames := selection_summary.text
	check(selection_summary.visible and summary_in_frames.begins_with("4 ") and summary_in_frames.contains("0.133"), "summary shows the inclusive frame count and duration")
	units.select(1)
	units.item_selected.emit(1)
	check(selection_summary.text == summary_in_frames, "changing the display unit keeps the same deletion summary")
	check(absf(last_frame.value - 13.0 / 30.0) < 0.001, "seconds show the time of the included frame 13")
	units.select(0)
	units.item_selected.emit(0)
	check(first_frame.value == 10 and last_frame.value == 13, "switching units preserves the inclusive selection")
	delete_frames.pressed.emit()
	var frame_cut := workbench.get_current_animation()
	check(is_equal_approx(frame_cut.length, 2.99 - 4.0 / 30.0), "10 through 13 deletes four frames")
	check(not frame_cut.has_marker("frame_10") and not frame_cut.has_marker("frame_13"), "markers at both selected endpoints are removed")
	check(frame_cut.has_marker("frame_9") and is_equal_approx(frame_cut.get_marker_time("frame_14"), 10.0 / 30.0), "neighboring frames are preserved")
	check(is_equal_approx(frame_cut.position_track_interpolate(position_track, 10 * sample_step).x, 14.0), "the first surviving pose follows the cut")
	check(delete_frames.disabled, "successful deletion clears the selection")
	check(not selection_summary.visible, "successful deletion hides the summary")
	check(first_frame.get_line_edit().text.is_empty() and last_frame.get_line_edit().text.is_empty(), "clearing the selection restores both placeholders")
	workbench.undo_edit()
	first_frame.value = 10
	last_frame.value = 10
	check(not delete_frames.disabled, "equal endpoints select one frame")
	delete_frames.pressed.emit()
	check(is_equal_approx(workbench.get_current_animation().length, 2.99 - 1.0 / 30.0), "a single frame is removed")
	workbench.undo_edit()
	first_frame.value = 89
	last_frame.value = 89
	check(selection_summary.text.begins_with("1 ") and selection_summary.text.contains("0.023"), "the truncated final frame shows its actual duration")
	delete_frames.pressed.emit()
	var tail_cut := workbench.get_current_animation()
	check(is_equal_approx(tail_cut.length, 89.0 / 30.0), "the final frame is trimmed to the preceding boundary")
	check(not tail_cut.has_marker("frame_89") and not tail_cut.has_marker("terminal"), "the discarded tail leaves no terminal marker")
	check(is_equal_approx(tail_cut.position_track_interpolate(position_track, tail_cut.length).x, 88.0), "the final pose comes from the last remaining frame")
	workbench.undo_edit()
	units.select(1)
	units.item_selected.emit(1)
	first_frame.value = 1.0
	last_frame.value = 2.0
	check(selection_summary.text.begins_with("31 ") and selection_summary.text.contains("1.033"), "1 to 2 seconds at 30 FPS previews 31 frames and 1.033 seconds")
	await settle(2)
	check(first_frame.get_line_edit().text.begins_with("1.000 ") and last_frame.get_line_edit().text.begins_with("2.000 "), "seconds fields display three places")
	units.select(0)
	units.item_selected.emit(0)
	check(first_frame.value == 30 and last_frame.value == 60, "1 to 2 seconds resolves to frames 30 through 60")
	units.select(1)
	units.item_selected.emit(1)
	check(is_equal_approx(first_frame.value, 1.0) and is_equal_approx(last_frame.value, 2.0), "changing display units preserves the selected endpoints")
	delete_frames.pressed.emit()
	var seconds_cut := workbench.get_current_animation()
	check(is_equal_approx(seconds_cut.length, 2.99 - 31.0 / 30.0), "1 to 2 seconds deletes 31 frames at 30 frames per second")
	check(not seconds_cut.has_marker("frame_30") and not seconds_cut.has_marker("frame_59") and not seconds_cut.has_marker("frame_60"), "seconds selection removes both boundary samples and interior samples")
	check(seconds_cut.has_marker("frame_29") and is_equal_approx(seconds_cut.get_marker_time("frame_61"), 1.0), "seconds selection preserves surrounding samples")
	workbench.undo_edit()
	first_frame.value = 2.0
	last_frame.value = 2.0
	check(not delete_frames.disabled, "equal endpoints in seconds select one sample")
	delete_frames.pressed.emit()
	check(is_equal_approx(workbench.get_current_animation().length, 2.99 - 1.0 / 30.0), "one sample is removed in seconds mode")
	workbench.undo_edit()

	begin("seconds selection includes both time endpoints without a sampling rate")
	workbench.select_animation(&"Door/Close")
	check(units.is_item_disabled(0) and units.selected == 1, "the clip has no frame grid")
	first_frame.value = 1.0
	last_frame.value = 2.0
	check(selection_summary.visible and selection_summary.text in ["1.000 s", "1.000 秒"], "duration without a sampling rate uses three decimal places")
	delete_frames.pressed.emit()
	var time_cut := workbench.get_current_animation()
	check(is_equal_approx(time_cut.length, 2.0), "continuous range trims the selected duration")
	check(not time_cut.has_marker("first") and not time_cut.has_marker("last"), "both endpoint markers are removed")
	check(is_equal_approx(time_cut.get_marker_time("after"), 1.1), "later markers shift by the removed duration")
	check(time_cut.track_get_key_count(value_track) == 2 and is_equal_approx(time_cut.track_get_key_value(value_track, 1), 2.1), "endpoint and interior keys are removed while the next key survives")
	workbench.undo_edit()
	workbench.select_animation(&"Open")
	check(units.selected == 1, "a manually chosen seconds display carries over to other clips")
	units.select(0)
	units.item_selected.emit(0)
	workbench.select_animation(&"Door/Close")
	check(units.selected == 1, "a clip without a sampling rate temporarily uses seconds")
	workbench.select_animation(&"Open")
	check(units.selected == 0, "returning to a sampled clip restores frames")

	begin("saving the owning scene persists a dirty built-in clip before packing")
	workbench.select_animation(&"Open")
	check(workbench.delete_range(0.0, 0.1) == OK, workbench.get_last_error())
	var draft_length: float = workbench.get_current_animation().length
	check(EditorInterface.save_scene_root(EditorInterface.get_edited_scene_root()) == OK, "scene saves with a pending draft")
	var saved_door := (ResourceLoader.load("res://door.tscn", "PackedScene", ResourceLoader.CACHE_MODE_IGNORE_DEEP) as PackedScene).instantiate()
	var saved_door_player := saved_door.get_node("AnimationPlayer") as AnimationPlayer
	check(is_equal_approx(saved_door_player.get_animation(&"Open").length, draft_length), "a cold read contains the edited clip")
	check(not workbench.get_state()["clip_modified"], "the written draft is clean")
	saved_door.free()

	begin("a clip embedded in a standalone library is saved to the library file")
	workbench.select_animation(&"Door/Close")
	check(workbench.delete_range(0.0, 0.1) == OK, workbench.get_last_error())
	var library_length: float = workbench.get_current_animation().length
	check(workbench.save_animation() == OK, workbench.get_last_error())
	var saved_library := ResourceLoader.load(library_path, "AnimationLibrary", ResourceLoader.CACHE_MODE_IGNORE_DEEP) as AnimationLibrary
	check(is_equal_approx(saved_library.get_animation(&"Close").length, library_length), "cold-loaded external library contains the edit")

	if OS.get_name() == "Windows" and EditorInterface.get_editor_settings().get_setting("filesystem/on_save/safe_save_on_backup_then_rename"):
		begin("safe-save reports a locked destination and retries without losing edits")
		for filename: String in ["clip.tres", "clip.res", "compressed.res"]:
			var test_path := "res://" + filename
			var test_clip := Animation.new()
			test_clip.length = 1.0
			var flags := ResourceSaver.FLAG_COMPRESS if filename == "compressed.res" else 0
			check(ResourceSaver.save(test_clip, test_path, flags) == OK, "original saved " + filename)
			var locked := FileAccess.open(test_path, FileAccess.READ)
			test_clip.length = 2.0
			check(ResourceSaver.save(test_clip, test_path, flags) != OK, "replacement failure is reported for " + filename)
			locked.close()
			check(is_equal_approx((cold(test_path) as Animation).length, 1.0), "previous file is intact for " + filename)
			check(ResourceSaver.save(test_clip, test_path, flags) == OK, "retry succeeds for " + filename)
			check(is_equal_approx((cold(test_path) as Animation).length, 2.0), "retry persisted the edit for " + filename)

	begin("a 2D scene keeps the native animation dock and its own main screen")
	var canvas_root := Node2D.new()
	canvas_root.name = "Menu"
	var canvas_player := AnimationPlayer.new()
	canvas_player.name = "AnimationPlayer"
	canvas_root.add_child(canvas_player)
	canvas_player.owner = canvas_root
	var canvas_scene := PackedScene.new()
	canvas_scene.pack(canvas_root)
	ResourceSaver.save(canvas_scene, "res://menu.tscn")
	canvas_root.free()
	EditorInterface.open_scene_from_path("res://menu.tscn")
	await settle(30)
	check(EditorInterface.get_active_editor_document() == "", "scene tab is active")
	var menu_player := EditorInterface.get_edited_scene_root().get_node("AnimationPlayer") as AnimationPlayer
	check(EditorInterface.open_animation_node(menu_player), "node opened")
	await settle(20)
	check(EditorInterface.get_active_editor_document() == "", "no central document for a 2D scene")
	var docks := EditorInterface.get_base_control().find_children("*", "AnimationPlayerEditor", true, false)
	check(docks.size() == 1 and (docks[0] as Control).is_visible_in_tree(), "native animation dock is shown")
	check(not (docks[0] as Control).get_parent().is_class("VSplitContainer"), "dock is outside the 3D document area")

	begin("clip resources select their entry in the owning skeleton document")
	workbench.open_resource(load(first_clips[0]))
	await settle(20)
	EditorInterface.edit_resource(load(skeleton_path))
	await settle(20)
	check(EditorInterface.get_active_editor_document() == skeleton_path, "skeleton opened from a clip document, active " + EditorInterface.get_active_editor_document())
	EditorInterface.edit_resource(load(clip_path))
	await settle(20)
	check(EditorInterface.get_active_editor_document() == skeleton_path, "clip keeps the skeleton document active, active " + EditorInterface.get_active_editor_document())
	check(workbench.get_state()["document"] == skeleton_path and workbench.get_state()["animation"] == "Run", "workbench selects Run in the skeleton catalog")
	EditorInterface.edit_resource(load(first_clips[0]))
	await settle(20)
	check(workbench.get_state()["document"] == skeleton_path and workbench.get_state()["animation"] == "Idle", "another clip selects its catalog entry")

	begin("the play switch survives leaving the workbench and editing a clip")
	workbench.set_playing(true)
	await settle(5)
	EditorInterface.activate_scene_document()
	await settle(12)
	workbench.open_resource(load(first_clips[0]))
	await settle(20)
	check(workbench.get_state()["playing"], "still playing after returning from a scene tab")
	var play_length: float = workbench.get_state()["length"]
	check(workbench.delete_range(0.0, play_length * 0.2) == OK, workbench.get_last_error())
	check(workbench.get_state()["playing"], "still playing after deleting a range")
	workbench.undo_edit()
	check(workbench.get_state()["playing"], "still playing after undo")
	workbench.set_playing(false)
	EditorInterface.activate_scene_document()
	await settle(12)
	workbench.open_resource(load(first_clips[0]))
	await settle(20)
	check(not workbench.get_state()["playing"], "a paused preview stays paused after returning")

	begin("a locked scene context still lets resource documents open")
	EditorInterface.open_scene_from_path("res://door.tscn")
	await settle(20)
	var lock_button := EditorInterface.get_base_control().find_child("DocumentContextLock", true, false) as Button
	var locked_root := EditorInterface.get_edited_scene_root()
	EditorInterface.get_selection().clear()
	EditorInterface.get_selection().add_node(locked_root)
	await settle(8)
	lock_button.button_pressed = true
	lock_button.pressed.emit()
	await settle(8)
	EditorInterface.edit_resource(load(skeleton_path))
	await settle(20)
	check(EditorInterface.get_active_editor_document() == skeleton_path, "skeleton document opened while locked, active " + EditorInterface.get_active_editor_document())
	check(EditorInterface.get_inspector().get_edited_object() == locked_root, "inspector stays on the locked scene")
	var shared_path := "res://lock_test.animtree"
	check(ResourceSaver.save(AnimationNodeBlendTree.new(), shared_path) == OK, "shared graph created")
	EditorInterface.edit_resource(load(shared_path))
	await settle()
	var context := get_tree().root.find_child("AnimationTreeDocumentContext", true, false)
	check(context != null and EditorInterface.get_active_editor_document() == shared_path, "shared graph active under scene lock")
	if context:
		var working_graph := (context.get_node("AnimationTree") as AnimationTree).tree_root
		var manager := EditorInterface.get_editor_undo_redo()
		var old_name := working_graph.resource_name
		manager.create_action("Edit locked workspace graph", UndoRedo.MERGE_DISABLE, working_graph)
		manager.add_do_property(working_graph, "resource_name", "Edited graph")
		manager.add_undo_property(working_graph, "resource_name", old_name)
		manager.commit_action()
		check(run_history_command("ui_undo") and working_graph.resource_name == old_name, "global Undo targets the shared graph")
		check(run_history_command("ui_redo") and working_graph.resource_name == "Edited graph", "global Redo targets the shared graph")
		run_history_command("ui_undo")
	lock_button.button_pressed = false
	lock_button.pressed.emit()
	await settle(8)

	begin("orphan clips require rebinding and cannot be saved without a skeleton")
	var orphan := Animation.new()
	orphan.length = 0.5
	var orphan_track := orphan.add_track(Animation.TYPE_ROTATION_3D)
	orphan.track_set_path(orphan_track, NodePath("Skeleton3D:Arm"))
	orphan.rotation_track_insert_key(orphan_track, 0.0, Quaternion())
	orphan.rotation_track_insert_key(orphan_track, 0.5, Quaternion(Vector3.RIGHT, 0.2))
	var orphan_source := "res://characters/orphan_source.tres"
	var orphan_path := "res://characters/orphan_rebind.skanim"
	check(ResourceSaver.save(orphan, orphan_source) == OK, "orphan source prepared")
	check(ResourceSaver.save(orphan, orphan_path) != OK, "an unbound .skanim cannot be saved")
	DirAccess.copy_absolute(ProjectSettings.globalize_path(orphan_source), ProjectSettings.globalize_path(orphan_path))
	ResourceSaver.set_uid(orphan_path, ResourceUID.create_id())
	var orphan_clip := cold(orphan_path) as Animation
	workbench.open_resource(orphan_clip)
	await settle(10)
	var rebind_state := workbench.get_state()
	check(rebind_state["rebind_pending"], "orphan opens the rebind dialog")
	check(rebind_state["rebind_clip"] == orphan_path, "rebind dialog identifies the orphan")
	check(rebind_state["document"] != orphan_path, "orphan does not become its own document")
	var rebound_skeleton := load("res://characters/copy.sk") as SkeletonDefinition
	var orphan_bytes := FileAccess.get_file_as_bytes(orphan_path)
	var orphan_uid := ResourceLoader.get_resource_uid(orphan_path)
	if not ResourceUID.has_id(orphan_uid):
		ResourceUID.add_id(orphan_uid, orphan_path)
	var blocked_path := rebound_skeleton.get_animation_directory_path().path_join("Blocked")
	var blocker := FileAccess.open(blocked_path, FileAccess.WRITE)
	blocker.store_string("A file occupies the intended library directory.")
	blocker.close()
	check(workbench.rebind_animation(orphan_clip, rebound_skeleton, "Blocked/ReboundClip") != OK, "blocked destination rejects rebind")
	check(orphan_clip.resource_path == orphan_path and orphan_clip.skeleton_path.is_empty(), "failed rebind keeps the in-memory owner and path")
	check(FileAccess.get_file_as_bytes(orphan_path) == orphan_bytes, "failed rebind leaves the source file byte-for-byte intact")
	DirAccess.remove_absolute(blocked_path)
	if OS.get_name() == "Windows":
		var locked_source := FileAccess.open(orphan_path, FileAccess.READ)
		check(workbench.rebind_animation(orphan_clip, rebound_skeleton, "ReboundClip") != OK, "a locked source cannot be replaced")
		locked_source.close()
		check(orphan_clip.resource_path == orphan_path and orphan_clip.skeleton_path.is_empty(), "locked-source failure keeps the in-memory owner and path")
		check(FileAccess.get_file_as_bytes(orphan_path) == orphan_bytes, "locked-source failure leaves the original bytes intact")
		check(ResourceUID.get_id_path(orphan_uid) == orphan_path, "failed rebind keeps the original UID mapping")
		check(not FileAccess.file_exists(rebound_skeleton.get_animation_directory_path().path_join("ReboundClip.skanim")), "locked-source failure leaves no destination clip")
	check(workbench.rebind_animation(orphan_clip, rebound_skeleton, "ReboundClip") == OK, workbench.get_last_error())
	check(not workbench.get_state()["rebind_pending"], "rebind dialog closes after success")
	var rebound_path := rebound_skeleton.get_animation_directory_path().path_join("ReboundClip.skanim")
	check(FileAccess.file_exists(rebound_path) and not FileAccess.file_exists(orphan_path), "orphan file moved into the animation directory as " + rebound_path)
	check((cold(rebound_path) as Animation).skeleton_path == rebound_skeleton.resource_path, "orphan clip now references its skeleton")
	check(ResourceLoader.get_resource_uid(rebound_path) == orphan_uid, "rebind preserves the clip UID")
	for directory: String in [orphan_path.get_base_dir(), rebound_path.get_base_dir()]:
		var files := DirAccess.open(directory)
		files.include_hidden = true
		check(not Array(files.get_files()).any(func(file: String) -> bool: return file.begins_with(".rebind-")), "rebind removes temporary files in " + directory)
	workbench.open_resource(cold(rebound_path))
	await settle(20)
	check(workbench.get_state()["document"] == rebound_skeleton.resource_path, "rebound clip opens its skeleton document")
	check(workbench.get_state()["animation"] == "ReboundClip", "rebound clip is selected in the catalog")

	begin("AnimationTree binds individual SKMesh instances and refreshes through undo and save")
	await verify_skmesh_binding(model_path)

	begin("creating an AnimationTree includes its embedded BlendTree in one scene action")
	await verify_animation_tree_defaults()

	begin("first scene save keeps a single player document and closing the scene removes it")
	await verify_first_scene_save()

	begin("an independent clip draft does not block closing a clean untitled scene")
	await verify_untitled_clip_scope(clip_path)

	begin("an untitled scene still prompts for its own workbench draft")
	await verify_owned_untitled_draft()

	begin("canceling or failing Save As abandons the pending quit")
	await verify_aborted_save_as()

	begin("a multi-skeleton GLB keeps completed groups when a later group fails")
	verify_partial_skeleton_import()

	begin("the editor process ends cleanly while the owning skeleton document is active")
	workbench.open_resource(load(clip_path))
	await settle(20)
	check(EditorInterface.get_active_editor_document() == skeleton_path, "skeleton document is active at exit")

	write_results()
	get_tree().quit(1 if results.any(func(entry: Dictionary) -> bool: return not entry["passed"]) else 0)


func verify_partial_skeleton_import() -> void:
	var pair := Node3D.new()
	pair.name = "Pair"
	for rig_name: String in ["LeftRig", "RightRig"]:
		var rig := build_rig(PackedStringArray([rig_name + "Root", rig_name + "Tip"]), true, {})
		rig.name = rig_name
		pair.add_child(rig)
		rig.owner = pair
	var pair_file := fixtures.path_join("pair.glb")
	write_glb(pair, pair_file)
	var options := {"create_skeleton": true}
	var probe := EditorInterface.import_animation_assets(pair_file, "res://pair_reference", options)
	var groups := reports(probe)
	check(probe.error == OK and groups.size() == 2, "fixture imports two separate skeletons")
	if groups.size() == 2:
		var target := "res://pair_partial"
		var blocked := target.path_join(String(groups[1].model).get_file())
		DirAccess.make_dir_recursive_absolute(blocked)
		var partial := EditorInterface.import_animation_assets(pair_file, target, options)
		var attempted := reports(partial)
		check(partial.error != OK and attempted.size() == 2, "the import reports the second-group failure")
		if attempted.size() == 2:
			check(attempted[0].completed and not attempted[1].completed, "the completed and failed groups are identified")
			check(partial.message.contains(attempted[1].source_skeleton), "the failed skeleton appears in the message")
			for saved: String in attempted[0].saved_files:
				check(FileAccess.file_exists(saved) and partial.message.contains(saved), "retained output " + saved)
			check(cold(attempted[0].model) is PackedScene and cold(attempted[0].definition) is SkeletonDefinition, "the first group's model and skeleton still load")
			var partial_definition := cold(attempted[1].definition) as SkeletonDefinition
			check(attempted[1].saved_files == [attempted[1].definition], "only the saved skeleton is reported for the failed group")
			check(partial_definition != null and partial_definition.preview_mesh.is_empty(), "the retained skeleton has no missing preview mesh")


func verify_first_scene_save() -> void:
	var root := await create_untitled_scene()
	if not root:
		return
	var player := AnimationPlayer.new()
	player.name = "Player"
	root.add_child(player)
	player.owner = root
	EditorInterface.get_animation_workbench().open_player(player)
	await settle()
	var original_key := EditorInterface.get_active_editor_document()
	var tabs := find_scene_tabs()
	var tab_count := tabs.tab_count
	var path := "res://first-save.tscn"
	EditorInterface.save_scene_as(path, false)
	check(root.scene_file_path == path, "scene has its saved path")
	EditorInterface.get_animation_workbench().open_player(player)
	await settle()
	check(EditorInterface.get_active_editor_document() == original_key and tabs.tab_count == tab_count, "reopening the same node reuses its tab")
	for index in tabs.tab_count:
		if tabs.get_tab_tooltip(index).ends_with("first-save.tscn"):
			tabs.tab_close_pressed.emit(index)
			break
	await settle()
	check(not EditorInterface.get_open_scenes().has(path), "saved scene closed")
	check(find_document_tab(tabs, original_key) < 0, "scene-owned tab was removed")


func verify_skmesh_binding(model_path: String) -> void:
	var path := "res://characters/binding_test.tscn"
	var scene := Node3D.new()
	scene.name = "Character"
	var model := load(model_path) as PackedScene
	for parent_name: String in ["Left", "Right"]:
		var parent := Node3D.new()
		parent.name = parent_name
		scene.add_child(parent)
		parent.owner = scene
		var instance := model.instantiate()
		instance.name = "Model"
		parent.add_child(instance)
		instance.owner = scene
	var native_player := AnimationPlayer.new()
	native_player.name = "RegularPlayer"
	scene.add_child(native_player)
	native_player.owner = scene
	var tree := AnimationTree.new()
	tree.name = "AnimationTree"
	tree.active = false
	var graph := AnimationNodeBlendTree.new()
	var animation_node := AnimationNodeAnimation.new()
	animation_node.animation = &"Idle"
	graph.add_node(&"Clip", animation_node)
	graph.connect_node(&"output", 0, &"Clip")
	tree.tree_root = graph
	scene.add_child(tree)
	tree.owner = scene
	var packed := PackedScene.new()
	check(packed.pack(scene) == OK and ResourceSaver.save(packed, path) == OK, "fixture saved")
	scene.free()

	EditorInterface.open_scene_from_path(path)
	await settle(20)
	scene = EditorInterface.get_edited_scene_root()
	tree = scene.get_node("AnimationTree")
	check(EditorInterface.open_animation_node(tree), "graph tab opened")
	await settle(20)
	var workspace := EditorInterface.get_base_control().find_child("AnimationTreeDocumentWorkspace", true, false)
	var skeleton_picker := workspace.find_children("*", "EditorResourcePicker", true, false)[0] as EditorResourcePicker
	var left_player := scene.get_node("Left/Model/AnimationPlayer") as AnimationPlayer
	check(skeleton_picker.edited_resource == null and not graph_offers_animation(workspace, "Idle"), "unbound graph has no skeleton or clip options")
	var property: EditorProperty
	for candidate: EditorProperty in EditorInterface.get_inspector().find_children("*", "EditorProperty", true, false):
		if candidate.get_edited_property() == &"anim_player":
			property = candidate
			break
	check(property != null, "Anim Player property is present")
	if not property:
		return
	var assign := property.find_children("*", "Button", true, false)[0] as Button
	assign.pressed.emit()
	await settle()
	var normal_picker := property.find_children("*", "SceneTreeDialog", true, false)[0] as ConfirmationDialog
	check(normal_picker.visible, "the original Assign picker opens")
	normal_picker.hide()

	await choose_skmesh_instance("Left/Model")
	check(tree.anim_player == NodePath("../Left/Model/AnimationPlayer"), "left instance path is relative to the tree")
	check(skeleton_picker.edited_resource == left_player.skeleton_definition and graph_offers_animation(workspace, "Idle"), "the open graph immediately displays the bound skeleton and clip options")
	await choose_skmesh_instance("Right/Model")
	check(tree.anim_player == NodePath("../Right/Model/AnimationPlayer"), "the second instance of the same SKMesh binds separately")
	check(tree.get_node_or_null(tree.root_node) == scene.get_node("Right/Model"), "tracks resolve under the selected model")

	var undo := get_undo_redo()
	var history := undo.get_history_undo_redo(undo.get_object_history_id(tree))
	check(history.undo(), "undo second binding")
	await settle()
	check(tree.anim_player == NodePath("../Left/Model/AnimationPlayer"), "undo restores left instance")
	check(history.undo(), "undo initial binding")
	await settle()
	check(tree.anim_player.is_empty() and skeleton_picker.edited_resource == null and not graph_offers_animation(workspace, "Idle"), "undo clears the binding, skeleton and clip options")
	check(history.redo(), "redo left binding")
	check(history.redo(), "redo right binding")
	await settle()
	check(tree.anim_player == NodePath("../Right/Model/AnimationPlayer") and graph_offers_animation(workspace, "Idle"), "redo restores the right instance and clip options")
	check(EditorInterface.save_scene_root(scene) == OK, "character scene saved")
	var reloaded := (cold(path) as PackedScene).instantiate()
	var saved_tree := reloaded.get_node("AnimationTree") as AnimationTree
	check(saved_tree.get_node_or_null(saved_tree.anim_player) == reloaded.get_node("Right/Model/AnimationPlayer"), "the saved scene resolves the selected instance")
	check(not reloaded.is_editable_instance(reloaded.get_node("Right/Model")), "binding works with the SKMesh instance's children kept private")
	reloaded.free()


func choose_skmesh_instance(path: String) -> void:
	var inspector := EditorInterface.get_inspector()
	var choose := inspector.find_child("ChooseSKMesh", true, false) as Button
	check(choose != null, "SKMesh entry is available")
	if not choose:
		return
	choose.pressed.emit()
	await settle()
	var picker := inspector.find_child("SKMeshInstancePicker", true, false) as ConfirmationDialog
	var instances := picker.find_child("SKMeshInstances", true, false) as Tree
	var rows := instances.get_root().get_children()
	check(rows.size() == 2 and rows[0].get_text(0) == rows[1].get_text(0), "picker lists both identically named model instances")
	for item: TreeItem in rows:
		if item.get_text(1) == path:
			item.select(0)
			instances.item_selected.emit()
			picker.get_ok_button().pressed.emit()
			await settle(20)
			return
	check(false, "missing SKMesh instance " + path)
	picker.hide()


func graph_offers_animation(workspace: Node, animation_name: String) -> bool:
	for menu: MenuButton in workspace.find_children("*", "MenuButton", true, false):
		var popup := menu.get_popup()
		for item: int in popup.item_count:
			if popup.get_item_text(item) == animation_name:
				return true
	return false


func verify_animation_tree_defaults() -> void:
	var path := "res://characters/tree_defaults.tscn"
	var scene := Node3D.new()
	scene.name = "Character"
	for branch_name: String in ["Left", "Right"]:
		var branch := Node3D.new()
		branch.name = branch_name
		scene.add_child(branch)
		branch.owner = scene
	var empty := AnimationTree.new()
	empty.name = "PreviouslyEmpty"
	empty.active = false
	scene.add_child(empty)
	empty.owner = scene
	var packed := PackedScene.new()
	check(packed.pack(scene) == OK and ResourceSaver.save(packed, path) == OK, "fixture saved")
	scene.free()
	EditorInterface.open_scene_from_path(path)
	await settle(20)
	scene = EditorInterface.get_edited_scene_root()

	if not await add_animation_tree_from_dock([scene]):
		return
	var tree := scene.get_node_or_null("AnimationTree") as AnimationTree
	check(tree != null, "Add Node creates an AnimationTree")
	if not tree:
		return
	var graph := tree.tree_root as AnimationNodeBlendTree
	check(graph != null and graph.is_built_in() and graph.has_node(&"output") and graph.get_node_list().size() == 1, "new node already has an embedded Output-only graph")
	check(EditorInterface.save_scene_root(scene) == OK, "scene saved before opening the graph")
	var reloaded := (cold(path) as PackedScene).instantiate()
	var reloaded_graph := (reloaded.get_node("AnimationTree") as AnimationTree).tree_root as AnimationNodeBlendTree
	check(reloaded_graph != null and reloaded_graph.is_built_in() and reloaded_graph.get_node_list().size() == 1 and reloaded_graph.has_node(&"output"), "saved scene restores the embedded graph")
	reloaded.free()

	var undo := get_undo_redo()
	var history := undo.get_history_undo_redo(undo.get_object_history_id(scene))
	var version := history.get_version()
	check(EditorInterface.open_animation_node(tree), "graph tab opened")
	await settle()
	check(tree.tree_root == graph and history.get_version() == version, "opening the tab leaves the graph and undo history intact")
	check(run_history_command("ui_undo") and not scene.has_node("AnimationTree"), "one undo removes the node")
	await settle()
	check(run_history_command("ui_redo") and scene.get_node_or_null("AnimationTree") == tree and tree.tree_root == graph, "one redo restores the node with its graph")
	await settle()

	empty = scene.get_node("PreviouslyEmpty") as AnimationTree
	version = history.get_version()
	check(EditorInterface.open_animation_node(empty), "an existing empty tree can still open")
	await settle()
	check(empty.tree_root == null and history.get_version() == version, "opening an existing empty tree does not create a graph or scene edit")
	EditorInterface.open_scene_from_path(path)
	await settle()

	var left := scene.get_node("Left")
	var right := scene.get_node("Right")
	if not await add_animation_tree_from_dock([left, right]):
		return
	var left_tree := left.get_node_or_null("AnimationTree") as AnimationTree
	var right_tree := right.get_node_or_null("AnimationTree") as AnimationTree
	check(left_tree != null and right_tree != null, "multi-add creates both nodes")
	if left_tree and right_tree:
		check(left_tree.tree_root is AnimationNodeBlendTree and right_tree.tree_root is AnimationNodeBlendTree and left_tree.tree_root != right_tree.tree_root, "each new node gets its own BlendTree")
		check(run_history_command("ui_undo") and not left.has_node("AnimationTree") and not right.has_node("AnimationTree"), "one undo removes both nodes")
		check(run_history_command("ui_redo") and left.get_node_or_null("AnimationTree") == left_tree and right.get_node_or_null("AnimationTree") == right_tree, "one redo restores both nodes")
		await settle()


func run_history_command(shortcut_name: String) -> bool:
	var shortcut := EditorInterface.get_editor_settings().get_shortcut(shortcut_name)
	for menu: PopupMenu in EditorInterface.get_base_control().find_children("*", "PopupMenu", true, false):
		for index: int in menu.item_count:
			if menu.get_item_shortcut(index) == shortcut:
				menu.id_pressed.emit(menu.get_item_id(index))
				return true
	return false


func add_animation_tree_from_dock(parents: Array[Node]) -> bool:
	var editor_selection := EditorInterface.get_selection()
	editor_selection.clear()
	for parent: Node in parents:
		editor_selection.add_node(parent)
	await settle()
	var dock := EditorInterface.get_base_control().find_children("*", "SceneTreeDock", true, false)[0]
	var settings := EditorInterface.get_editor_settings()
	var shortcut := settings.get_shortcut("scene_tree/add_child_node")
	settings.set_project_metadata("dialog_bounds", "create_new_node", Rect2())
	for button: Button in dock.find_children("*", "Button", true, false):
		if button.shortcut == shortcut:
			button.pressed.emit()
			break
	await settle()
	var dialog := dock.find_children("*", "CreateDialog", true, false)[0] as ConfirmationDialog
	check(dialog.visible, "Add Node dialog opened")
	if not dialog.visible:
		return false
	var search := dialog.find_children("*", "FilterLineEdit", true, false)[0] as LineEdit
	search.text = "AnimationTree"
	search.text_changed.emit(search.text)
	await settle()
	check(not dialog.get_ok_button().disabled, "AnimationTree is available in Add Node")
	if dialog.get_ok_button().disabled:
		dialog.hide()
		return false
	dialog.get_ok_button().pressed.emit()
	await settle()
	return true


func find_scene_tabs() -> TabBar:
	for node: Node in get_tree().root.find_children("*", "TabBar", true, false):
		var parent := node.get_parent()
		while parent:
			if parent.is_class("EditorSceneTabs"):
				return node as TabBar
			parent = parent.get_parent()
	return null


func find_document_tab(p_tabs: TabBar, p_path: String) -> int:
	if not p_tabs:
		return -1
	for index: int in p_tabs.tab_count:
		if p_tabs.get_tab_metadata(index) == "resource:" + p_path:
			return index
	return -1


func click_tab_close(p_tabs: TabBar, p_tab: int) -> void:
	var tab_rect := p_tabs.get_tab_rect(p_tab)
	var position := p_tabs.get_global_position() + Vector2(tab_rect.end.x - 10.0, tab_rect.size.y * 0.5)
	for pressed: bool in [true, false]:
		var event := InputEventMouseButton.new()
		event.button_index = MOUSE_BUTTON_LEFT
		event.position = position
		event.global_position = position
		event.pressed = pressed
		Input.parse_input_event(event)
		await get_tree().process_frame


func find_clip_list() -> Tree:
	var nodes := get_tree().root.find_children("ClipList", "Tree", true, false)
	return nodes[0] as Tree if not nodes.is_empty() else null


func find_clip_item(p_list: Tree, p_name: StringName) -> TreeItem:
	if p_list == null or p_list.get_root() == null:
		return null
	for group: TreeItem in p_list.get_root().get_children():
		for item: TreeItem in group.get_children():
			if item.get_metadata(0) is StringName and item.get_metadata(0) == p_name:
				return item
	return null


func create_untitled_scene() -> Node:
	check(run_history_command("editor/new_scene"), "New Scene menu action exists")
	await settle()
	var dock := EditorInterface.get_base_control().find_children("*", "SceneTreeDock", true, false)[0]
	for button: Button in dock.find_children("*", "Button", true, false):
		if button.is_visible_in_tree() and button.icon == button.get_theme_icon("Node3D", "EditorIcons"):
			button.pressed.emit()
			break
	await settle()
	var root := EditorInterface.get_edited_scene_root()
	if not root is Node3D or not root.scene_file_path.is_empty():
		check(false, "created an untitled 3D scene")
		return null
	return root


func verify_untitled_clip_scope(clip_path: String) -> void:
	var root := await create_untitled_scene()
	if not root:
		return
	var undo := EditorInterface.get_editor_undo_redo()
	undo.clear_history(undo.get_object_history_id(root), false)
	var workbench := EditorInterface.get_animation_workbench()
	workbench.open_resource(load(clip_path))
	await settle()
	check(workbench.delete_range(0.0, 0.1) == OK, workbench.get_last_error())
	EditorInterface.activate_scene_document()
	await settle()
	var tabs := find_scene_tabs()
	tabs.tab_close_pressed.emit(tabs.current_tab)
	await settle()
	check(not is_instance_valid(root), "the clean untitled scene closed without a global save prompt")
	workbench.open_resource(load(clip_path))
	await settle()
	check(workbench.get_state()["clip_modified"], "the independent clip stayed dirty")
	workbench.undo_edit()


func visible_confirmation() -> ConfirmationDialog:
	for dialog: ConfirmationDialog in get_tree().root.find_children("*", "ConfirmationDialog", true, false):
		if dialog.visible:
			return dialog
	return null


func verify_owned_untitled_draft() -> void:
	var root := await create_untitled_scene()
	if not root:
		return
	var player := AnimationPlayer.new()
	root.add_child(player)
	player.owner = root
	var library := AnimationLibrary.new()
	library.add_animation(&"Local", Animation.new())
	player.add_animation_library(&"", library)
	var workbench := EditorInterface.get_animation_workbench()
	workbench.open_player(player)
	await settle()
	workbench.select_animation(&"Local")
	check(workbench.set_loop_mode(Animation.LOOP_LINEAR) == OK, "edited an embedded clip")
	var undo := EditorInterface.get_editor_undo_redo()
	undo.clear_history(undo.get_object_history_id(root), false)
	EditorInterface.activate_scene_document()
	await settle()
	var tabs := find_scene_tabs()
	tabs.tab_close_pressed.emit(tabs.current_tab)
	await settle()
	var dialog := visible_confirmation()
	check(dialog != null and is_instance_valid(root), "the owning untitled scene prompts before closing")
	if dialog:
		dialog.get_cancel_button().pressed.emit()
		await settle()
	if is_instance_valid(root):
		EditorInterface.save_scene_as("res://owned-draft.tscn", false)
		var saved := (cold("res://owned-draft.tscn") as PackedScene).instantiate()
		var saved_player := saved.find_children("*", "AnimationPlayer", true, false)[0] as AnimationPlayer
		check(saved_player.get_animation(&"Local").loop_mode == Animation.LOOP_LINEAR, "first save persists its own draft")
		saved.free()


func verify_aborted_save_as() -> void:
	# Only this scene should require confirmation in the quit sequence.
	for scene in EditorInterface.get_open_scene_roots():
		if not scene.scene_file_path.is_empty():
			check(EditorInterface.save_scene_root(scene) == OK, "save previous fixture")
	var root := await create_untitled_scene()
	if not root:
		return
	root.editor_description = "pending Save As"
	EditorInterface.mark_scene_as_unsaved()
	DirAccess.make_dir_recursive_absolute("res://blocked.tscn")
	for cancel: bool in [true, false]:
		EditorInterface.open_scene_from_path("res://door.tscn")
		await settle()
		check(run_history_command("editor/file_quit"), "request quit")
		await settle()
		var dialog := visible_confirmation()
		if not dialog:
			check(false, "quit prompts to save the untitled scene")
			return
		dialog.get_ok_button().pressed.emit()
		await settle()
		var picker := visible_confirmation() as FileDialog
		if not picker:
			check(false, "Save As file dialog appeared")
			return
		if cancel:
			picker.get_cancel_button().pressed.emit()
		else:
			picker.hide()
			picker.file_selected.emit("res://blocked.tscn")
		await settle()
		for warning: AcceptDialog in get_tree().root.find_children("*", "AcceptDialog", true, false):
			if warning.visible:
				warning.hide()
		var tabs := find_scene_tabs()
		for index in tabs.tab_count:
			if tabs.get_tab_tooltip(index).ends_with("door.tscn"):
				tabs.tab_close_pressed.emit(index)
				break
		await settle()
		check(is_instance_valid(root) and root.scene_file_path.is_empty(), "untitled scene survives a later independent close")
		check(not EditorInterface.get_open_scenes().has("res://door.tscn"), "only the requested tab closed")
	if is_instance_valid(root):
		EditorInterface.save_scene_as("res://recovered.tscn", false)
		check(root.scene_file_path == "res://recovered.tscn", "Save As can be retried")
