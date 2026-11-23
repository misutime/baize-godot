extends SceneTree


func _initialize() -> void:
	check_pack.call_deferred()


func check_pack() -> void:
	var scene := load("res://main.tscn") as PackedScene
	if scene == null:
		printerr("The selected scene could not be loaded from the pack.")
		quit(1)
		return
	var actor := scene.instantiate()
	root.add_child(actor)
	var player := actor.get_node_or_null("Model/AnimationPlayer") as AnimationPlayer
	var tree := actor.get_node_or_null("AnimationTree") as AnimationTree
	var skeleton := actor.get_node_or_null("Model/Skeleton3D") as Skeleton3D
	var rebound := OS.get_cmdline_user_args().has("rebound")
	var passed := (
		player != null
		and player.skeleton_definition != null
		and player.skeleton_definition.has_baked_animation_libraries()
		and player.has_animation(&"Idle")
		and player.has_animation(&"Combat/Jump") == (not rebound)
		and tree != null
		and tree.get_node_or_null(tree.root_node) == actor.get_node("Model")
		and tree.has_animation(&"Idle" if rebound else &"Combat/Jump")
		and skeleton != null
		and not ResourceLoader.exists("res://characters/copy.sk")
	)
	if passed:
		var bone := skeleton.find_bone("Arm")
		tree.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_MANUAL
		if rebound:
			(tree.tree_root as AnimationNodeAnimation).animation = &"Idle"
		tree.active = true
		tree.advance(0.0)
		var start_pose := skeleton.get_bone_pose_rotation(bone)
		tree.advance(0.25)
		passed = not skeleton.get_bone_pose_rotation(bone).is_equal_approx(start_pose)
	actor.free()
	if passed:
		print("REBOUND_ANIMATION_PACK_PASS" if rebound else "SELECTED_ANIMATION_PACK_PASS")
	else:
		printerr("The pack must contain both animation libraries and the character's tree must animate the model.")
	quit(0 if passed else 1)
