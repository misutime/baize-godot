@tool
extends EditorPlugin

var results: Array[Dictionary] = []
var current := ""
var failures := PackedStringArray()
var out_dir := ""
var resource_document: Resource
var resource_document_b: Resource


func _enter_tree() -> void:
	get_tree().create_timer(120.0).timeout.connect(abort)
	run.call_deferred()


func abort() -> void:
	check(false, "the run stopped inside this scenario; see the editor log")
	write_results()
	get_tree().quit(1)


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
		current = ""


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
	print("DOCUMENT_CONTEXT_REGRESSION scenarios=%d failed=%d" % [results.size(), failed.size()])


func settle(frames := 8) -> void:
	for i in frames:
		await get_tree().process_frame


func make_scene(path: String, root_name: String, marker_name: String) -> void:
	var root := Node3D.new()
	root.name = root_name
	var marker := Node3D.new()
	marker.name = marker_name
	root.add_child(marker)
	marker.owner = root
	var packed := PackedScene.new()
	check(packed.pack(root) == OK, "pack " + path)
	check(ResourceSaver.save(packed, path) == OK, "save " + path)
	root.free()


func inspect_context_resource() -> void:
	EditorInterface.inspect_object(resource_document, "", true)


func inspect_context_resource_b() -> void:
	EditorInterface.inspect_object(resource_document_b, "", true)


func no_inspector_context() -> void:
	pass


func node_names(nodes: Array) -> PackedStringArray:
	var names := PackedStringArray()
	for node: Node in nodes:
		names.push_back(String(node.name))
	return names


func scene_tree_root_name() -> String:
	var tree := EditorInterface.get_base_control().find_child("SceneTreeEditorTree", true, false) as Tree
	if not tree or not tree.get_root():
		return ""
	var root := tree.get_root()
	if not root.get_text(0).is_empty():
		return String(root.get_text(0))
	var children := root.get_children()
	return String(children[0].get_text(0)) if not children.is_empty() else ""


func find_scene_tabs() -> TabBar:
	for node: Node in get_tree().root.find_children("*", "TabBar", true, false):
		var parent := node.get_parent()
		while parent:
			if parent.is_class("EditorSceneTabs"):
				return node as TabBar
			parent = parent.get_parent()
	return null


func find_document_tab(p_tabs: TabBar, p_path: String) -> int:
	for index: int in p_tabs.tab_count:
		if p_tabs.get_tab_metadata(index) == "resource:" + p_path:
			return index
	return -1


func click_tab(p_tabs: TabBar, p_position: Vector2, p_button: MouseButton) -> void:
	var position := p_tabs.get_global_transform_with_canvas() * p_position
	var motion := InputEventMouseMotion.new()
	motion.position = position
	motion.global_position = position
	Input.parse_input_event(motion)
	for pressed: bool in [true, false]:
		var click := InputEventMouseButton.new()
		click.button_index = p_button
		click.pressed = pressed
		click.position = position
		click.global_position = position
		Input.parse_input_event(click)
	Input.flush_buffered_events()
	await settle()


func check_pinned_tab(p_tabs: TabBar, p_path: String) -> void:
	var tab := find_document_tab(p_tabs, p_path)
	check(tab >= 0, "document tab exists before pinning")
	if tab < 0:
		return
	get_tree().root.size = Vector2i(1440, 900)
	var scene_tabs := p_tabs.get_parent().get_parent().get_parent().get_parent()
	scene_tabs.set_tab_pinned(tab, true)
	p_tabs.tab_close_display_policy = TabBar.CLOSE_BUTTON_SHOW_ALWAYS
	await settle()
	tab = find_document_tab(p_tabs, p_path)
	var rect := p_tabs.get_tab_rect(tab)
	check(tab == 0 and scene_tabs.is_tab_pinned(tab), "pinned document is at the front")
	check(p_tabs.get_tab_button_icon(tab) == null, "pinned document has no inline pin button")
	p_tabs.tab_close_display_policy = TabBar.CLOSE_BUTTON_SHOW_NEVER
	await settle()
	check(p_tabs.get_tab_rect(tab).size == rect.size, "pinned document reserves no space for a close button")
	p_tabs.tab_close_display_policy = TabBar.CLOSE_BUTTON_SHOW_ALWAYS
	await settle()
	var margin := p_tabs.get_theme_stylebox("tab_selected").get_margin(SIDE_RIGHT)
	var close_width := p_tabs.get_theme_icon("close").get_width()
	var trailing_position := Vector2(rect.end.x - margin - close_width / 2.0, rect.get_center().y)
	await click_tab(p_tabs, trailing_position, MOUSE_BUTTON_LEFT)
	check(p_tabs.current_tab == tab and scene_tabs.is_tab_pinned(tab), "clicking the right side selects the document and keeps it pinned")
	await click_tab(p_tabs, rect.get_center(), MOUSE_BUTTON_MIDDLE)
	check(find_document_tab(p_tabs, p_path) == tab, "middle click leaves the pinned document open")
	if find_document_tab(p_tabs, p_path) != tab:
		return
	await click_tab(p_tabs, rect.get_center(), MOUSE_BUTTON_RIGHT)
	var menus := p_tabs.get_parent().get_parent().find_children("*", "PopupMenu", false, false)
	var menu := menus[0] as PopupMenu if not menus.is_empty() else null
	check(menu != null and menu.visible and menu.item_count > 0, "right click opens the tab menu")
	if not menu or not menu.visible or menu.item_count == 0:
		return
	menu.id_pressed.emit(menu.get_item_id(0))
	menu.hide()
	await settle()
	tab = find_document_tab(p_tabs, p_path)
	check(not scene_tabs.is_tab_pinned(tab), "the tab menu unpins the document")
	check(p_tabs.get_tab_rect(tab).size.x > rect.size.x, "unpinning restores the close button space")

	scene_tabs.set_tab_pinned(tab, true)
	await settle()
	await click_tab(p_tabs, p_tabs.get_tab_rect(0).get_center(), MOUSE_BUTTON_RIGHT)
	var close_shortcut := EditorInterface.get_editor_settings().get_shortcut("editor/close_scene")
	for index in menu.item_count:
		if menu.get_item_shortcut(index) == close_shortcut:
			menu.id_pressed.emit(menu.get_item_id(index))
			menu.hide()
			await settle()
			check(find_document_tab(p_tabs, p_path) < 0, "the tab menu closes a pinned document")
			return
	check(false, "the pinned tab menu offers Close Tab")


func run_history_command(shortcut_name: String) -> bool:
	var shortcut := EditorInterface.get_editor_settings().get_shortcut(shortcut_name)
	for menu: PopupMenu in EditorInterface.get_base_control().find_children("*", "PopupMenu", true, false):
		for index: int in menu.item_count:
			if menu.get_item_shortcut(index) == shortcut:
				menu.id_pressed.emit(menu.get_item_id(index))
				return true
	return false


func inspector_read_only() -> bool:
	var properties := EditorInterface.get_inspector().find_children("*", "EditorProperty", true, false)
	return properties.size() > 0 and (properties[0] as EditorProperty).read_only


func run() -> void:
	var arguments := OS.get_cmdline_user_args()
	out_dir = arguments[0] if arguments.size() > 0 else OS.get_user_data_dir()
	DirAccess.make_dir_recursive_absolute(out_dir)
	while EditorInterface.get_resource_filesystem().is_scanning():
		await get_tree().process_frame
	await settle(24)

	begin("restoring the script workspace shows line starts and keeps the edit position")
	var script_editor := EditorInterface.get_script_editor()
	var restored_script := script_editor.get_current_script()
	var restored_editor := script_editor.get_current_editor()
	var code := restored_editor.get_base_editor() as CodeEdit if restored_editor else null
	check(EditorInterface.get_active_editor_document() == "editor://scripts", "the script workspace is active")
	check(restored_script != null and restored_script.resource_path == "res://restore_script.gd", "the saved script opens")
	check(code != null and code.scroll_horizontal == 0, "line starts are visible")
	check(code != null and is_equal_approx(code.scroll_vertical, 8.0), "the saved vertical scroll is restored")
	check(code != null and code.get_caret_line() == 20 and code.get_caret_column() == 44, "the saved caret is restored")

	make_scene("res://scene_a.tscn", "SceneA", "MarkerA")
	make_scene("res://scene_b.tscn", "SceneB", "MarkerB")
	resource_document = Animation.new()
	resource_document.resource_name = "ContextResource"
	resource_document_b = Animation.new()
	resource_document_b.resource_name = "ContextResourceB"
	while EditorInterface.get_resource_filesystem().is_scanning():
		await get_tree().process_frame
	await settle(12)

	var lock_button := EditorInterface.get_base_control().find_child("DocumentContextLock", true, false) as Button
	var external_context := EditorInterface.get_base_control().find_child("ExternalDocumentContext", true, false) as Label
	check(lock_button != null, "document context lock button exists")
	check(external_context != null, "external document context placeholder exists")

	begin("opening a document without a scene object clears scene-owned context")
	EditorInterface.open_scene_from_path("res://scene_a.tscn")
	await settle(12)
	var scene_a := EditorInterface.get_edited_scene_root()
	var marker_a := scene_a.get_node_or_null("MarkerA") if scene_a else null
	check(scene_a != null and scene_a.name == "SceneA", "scene A is current")
	check(marker_a != null, "scene A marker exists")
	EditorInterface.get_selection().clear()
	if marker_a:
		EditorInterface.get_selection().add_node(marker_a)
	await settle()
	check(EditorInterface.get_inspector().get_edited_object() == marker_a, "scene A marker is inspected")
	EditorInterface.open_editor_document("editor://context-none", "ContextNone", null, no_inspector_context, Callable(), Callable(), Callable(), Callable(), true)
	await settle()
	check(EditorInterface.get_active_editor_document() == "editor://context-none", "no-context document is active")
	check(EditorInterface.get_inspector().get_edited_object() == null, "old scene object is not inspected in the document")
	check(EditorInterface.get_selection().get_selected_nodes().is_empty(), "old scene selection is cleared")
	check(external_context != null and external_context.visible, "scene tree announces the missing scene context")

	begin("returning to a scene restores its selection and inspector")
	EditorInterface.activate_scene_document()
	await settle(10)
	check(EditorInterface.get_active_editor_document().is_empty(), "scene document is active again")
	check(EditorInterface.get_inspector().get_edited_object() == marker_a, "scene A marker is inspected again")
	check(node_names(EditorInterface.get_selection().get_selected_nodes()).has("MarkerA"), "scene A selection is restored")
	check(external_context != null and not external_context.visible, "missing-context placeholder is hidden")

	begin("resource inspection is tied to the resource document")
	EditorInterface.open_editor_document("editor://context-resource", "ContextResource", null, inspect_context_resource, Callable(), Callable(), Callable(), Callable(), true)
	await settle()
	check(EditorInterface.get_active_editor_document() == "editor://context-resource", "resource document is active")
	check(EditorInterface.get_inspector().get_edited_object() == resource_document, "resource is inspected")
	check(EditorInterface.get_selection().get_selected_nodes().is_empty(), "resource document has no scene selection")
	check(external_context != null and external_context.visible, "scene tree shows resource document context")
	EditorInterface.activate_scene_document()
	await settle(10)
	check(EditorInterface.get_inspector().get_edited_object() == marker_a, "resource document does not leak into scene A")

	begin("each resource document restores its own context and inspector state")
	EditorInterface.open_editor_document("editor://context-resource", "ContextResource", null, inspect_context_resource, Callable(), Callable(), Callable(), Callable(), true)
	await settle()
	EditorInterface.open_editor_document("editor://context-resource-b", "ContextResourceB", null, inspect_context_resource_b, Callable(), Callable(), Callable(), Callable(), true)
	await settle()
	check(EditorInterface.get_inspector().get_edited_object() == resource_document_b, "second resource document inspects its own object")
	EditorInterface.open_editor_document("editor://context-resource", "ContextResource", null, inspect_context_resource, Callable(), Callable(), Callable(), Callable(), true)
	await settle()
	check(EditorInterface.get_active_editor_document() == "editor://context-resource", "first resource document is active again")
	check(EditorInterface.get_inspector().get_edited_object() == resource_document, "first resource document restores its object")
	EditorInterface.activate_scene_document()
	await settle(10)

	begin("closing the active document returns to the scene context")
	EditorInterface.remove_editor_document("editor://context-none")
	EditorInterface.remove_editor_document("editor://context-resource")
	await settle()
	EditorInterface.open_editor_document("editor://context-resource-b", "ContextResourceB", null, inspect_context_resource_b, Callable(), Callable(), Callable(), Callable(), true)
	await settle()
	check(EditorInterface.get_active_editor_document() == "editor://context-resource-b", "document is active before closing")
	EditorInterface.remove_editor_document("editor://context-resource-b")
	await settle(10)
	check(EditorInterface.get_active_editor_document().is_empty(), "closing the active document activates the scene")
	check(EditorInterface.get_inspector().get_edited_object() == marker_a, "closing the active document restores scene A selection")

	begin("one close-button event closes the active document")
	EditorInterface.open_editor_document("editor://context-resource", "ContextResource", null, inspect_context_resource, Callable(), Callable(), Callable(), Callable(), true)
	await settle()
	var tabs := find_scene_tabs()
	var document_tab := find_document_tab(tabs, "editor://context-resource") if tabs else -1
	check(tabs != null, "scene tab bar exists")
	check(document_tab >= 0, "resource tab is present")
	check(EditorInterface.get_active_editor_document() == "editor://context-resource", "resource tab is active before close")
	if tabs and document_tab >= 0:
		tabs.tab_close_pressed.emit(document_tab)
	await settle(10)
	check(EditorInterface.get_active_editor_document().is_empty(), "one close signal closes the document")
	check(tabs != null and find_document_tab(tabs, "editor://context-resource") < 0, "closed tab is removed on the first request")

	begin("scene tree lock keeps the pinned hierarchy and inspector")
	EditorInterface.get_selection().clear()
	EditorInterface.get_selection().add_node(marker_a)
	await settle()
	check(EditorInterface.get_inspector().get_edited_object() == marker_a, "marker A is pinned before locking")
	var undo_redo := EditorInterface.get_editor_undo_redo()
	var history_a := undo_redo.get_object_history_id(marker_a)
	undo_redo.create_action("Rename locked marker")
	undo_redo.add_do_property(marker_a, "editor_description", "edited while locked")
	undo_redo.add_undo_property(marker_a, "editor_description", marker_a.editor_description)
	undo_redo.commit_action()
	if lock_button:
		lock_button.button_pressed = true
		lock_button.pressed.emit()
	await settle()
	check(lock_button != null and lock_button.button_pressed, "lock button reports the locked state")
	check(lock_button != null and (lock_button.tooltip_text.to_lower().contains("locked") or lock_button.tooltip_text.contains("锁定")), "lock button tooltip reports the locked state: " + (lock_button.tooltip_text if lock_button else ""))
	EditorInterface.open_scene_from_path("res://scene_b.tscn")
	await settle(12)
	var scene_b := EditorInterface.get_edited_scene_root()
	var marker_b := scene_b.get_node_or_null("MarkerB") if scene_b else null
	check(scene_b != null and scene_b.name == "SceneB", "scene B is current while locked")
	check(scene_tree_root_name() == "SceneA", "locked scene tree still displays scene A, got " + scene_tree_root_name())
	var after_scene_b := EditorInterface.get_inspector().get_edited_object()
	check(after_scene_b == marker_a, "locked inspector ignores scene B, got " + (String(after_scene_b.name) if after_scene_b is Node else str(after_scene_b)))
	check(marker_b != null, "scene B marker exists")
	EditorInterface.get_selection().clear()
	if marker_b:
		EditorInterface.get_selection().add_node(marker_b)
	await settle()
	check(EditorInterface.get_inspector().get_edited_object() == marker_a, "locked inspector ignores selection changes")
	check(scene_tree_root_name() == "SceneA", "locked scene tree ignores scene B selection changes")
	var locked_tree := EditorInterface.get_base_control().find_child("SceneTreeEditorTree", true, false) as Tree
	locked_tree.nothing_selected.emit()
	check(EditorInterface.get_selection().get_selected_nodes().has(marker_b), "clicking empty space in the reference tree keeps the active scene selection")
	var locked_item := locked_tree.get_root()
	if locked_item:
		locked_tree.multi_selected.emit(locked_item, 0, true)
	check(EditorInterface.get_selection().get_selected_nodes() == [marker_b], "reference tree selection never inserts a detached node")
	check(inspector_read_only(), "the locked scene is read-only while another scene is current")
	check(undo_redo.get_object_history_id(marker_a) == history_a, "locked scene nodes keep their own scene history, got %d instead of %d" % [undo_redo.get_object_history_id(marker_a), history_a])
	var history_b := undo_redo.get_object_history_id(marker_b) if marker_b else -1
	check(history_b != history_a, "the two scenes use different histories")
	check(undo_redo.get_history_undo_redo(history_a).has_undo(), "the reference scene retains its previous history")
	check(history_b < 0 or not undo_redo.get_history_undo_redo(history_b).has_undo(), "the active scene history stays untouched")
	undo_redo.create_action("Edit active marker")
	undo_redo.add_do_property(marker_b, "editor_description", "active edit")
	undo_redo.add_undo_property(marker_b, "editor_description", marker_b.editor_description)
	undo_redo.commit_action()
	check(run_history_command("ui_undo") and marker_b.editor_description.is_empty(), "editor Undo targets the active scene")
	check(marker_a.editor_description == "edited while locked", "global Undo leaves the locked reference intact")
	check(run_history_command("ui_redo") and marker_b.editor_description == "active edit", "editor Redo targets the active scene")
	run_history_command("ui_undo")
	if lock_button:
		lock_button.button_pressed = false
		lock_button.pressed.emit()
	await settle(10)
	check(EditorInterface.get_inspector().get_edited_object() == marker_b, "unlocking follows the active scene selection")
	check(not inspector_read_only(), "unlocking makes the inspector editable again")
	check(node_names(EditorInterface.get_selection().get_selected_nodes()).has("MarkerB"), "scene B selection remains active")
	check(scene_tree_root_name() == "SceneB", "unlocking follows the active scene tree")

	begin("document context remains valid after removing test documents")
	EditorInterface.remove_editor_document("editor://context-none")
	EditorInterface.remove_editor_document("editor://context-resource")
	EditorInterface.remove_editor_document("editor://context-resource-b")
	await settle(10)
	check(EditorInterface.get_active_editor_document().is_empty(), "removing documents returns to the scene")
	check(scene_b != null and EditorInterface.get_edited_scene_root() == scene_b, "scene B remains current")

	begin("switching documents keeps tab geometry stable with active-only close buttons")
	if tabs:
		tabs.tab_close_display_policy = TabBar.CLOSE_BUTTON_SHOW_ACTIVE_ONLY
		EditorInterface.open_editor_document("editor://context-resource", "ContextResource", null, inspect_context_resource, Callable(), Callable(), Callable(), Callable(), true)
		await settle()
		var before: Array[Rect2] = []
		for index in tabs.tab_count:
			before.push_back(tabs.get_tab_rect(index))
		var minimum_width := tabs.get_minimum_size().x
		var active_tab := tabs.current_tab

		EditorInterface.activate_scene_document()
		await settle()
		check(tabs.current_tab != active_tab, "switching to the scene selects a different tab")
		check(tabs.get_minimum_size().x == minimum_width, "the tab bar keeps its minimum width")
		for index in before.size():
			check(tabs.get_tab_rect(index) == before[index], "tab %d keeps its position and width: %s -> %s" % [index, before[index], tabs.get_tab_rect(index)])
	else:
		check(false, "scene tab bar exists")

	begin("pinned documents select safely and unpin from the tab menu")
	if tabs:
		await check_pinned_tab(tabs, "editor://context-resource")
	else:
		check(false, "scene tab bar exists")

	begin("closing the background edited scene preserves the resource workspace")
	EditorInterface.save_scene_as("res://scene_b.tscn", false)
	EditorInterface.open_editor_document("editor://context-resource", "ContextResource", null, inspect_context_resource, Callable(), Callable(), Callable(), Callable(), true)
	await settle()
	for index in tabs.tab_count:
		if tabs.get_tab_tooltip(index).ends_with("scene_b.tscn"):
			tabs.tab_close_pressed.emit(index)
			break
	await settle(15)
	check(not EditorInterface.get_open_scenes().has("res://scene_b.tscn"), "background scene was closed")
	check(EditorInterface.get_active_editor_document() == "editor://context-resource", "resource tab remains active")
	check(EditorInterface.get_inspector().get_edited_object() == resource_document, "resource inspector is restored")

	if OS.get_name() == "Windows" and EditorInterface.get_editor_settings().get_setting("filesystem/on_save/safe_save_on_backup_then_rename"):
		EditorInterface.activate_scene_document()
		await settle()
		for scene in EditorInterface.get_open_scene_roots():
			if not scene.scene_file_path.is_empty():
				check(EditorInterface.save_scene_root(scene) == OK, "earlier fixture saved")
		for action: String in ["editor/reload_saved_scene", "editor/close_all_scenes", "editor/file_quit"]:
			begin("failed save cancels " + action + " and permits a retry")
			await check_failed_scene_save(action)

	write_results()
	var failed := results.filter(func(entry: Dictionary) -> bool: return not entry["passed"])
	get_tree().quit(0 if failed.is_empty() else 1)


func check_failed_scene_save(action: String) -> void:
	var path := "res://" + action.get_file() + ".tscn"
	var other := "res://" + action.get_file() + "_other.tscn"
	make_scene(path, "Pending", "Marker")
	make_scene(other, "Other", "Marker")
	EditorInterface.open_scene_from_path(path)
	await settle()
	var scene := EditorInterface.get_edited_scene_root()
	scene.editor_description = "pending edit"
	EditorInterface.mark_scene_as_unsaved()
	if action != "editor/reload_saved_scene":
		EditorInterface.open_scene_from_path(other)
		await settle()
	var lock := FileAccess.open(path, FileAccess.READ)
	check(lock != null, "hold target file open")
	check(run_history_command(action), "menu command dispatched")
	await settle()
	var confirmed := false
	for dialog: ConfirmationDialog in get_tree().root.find_children("*", "ConfirmationDialog", true, false):
		if dialog.visible:
			confirmed = true
			dialog.get_ok_button().pressed.emit()
			break
	check(confirmed, "save confirmation appeared")
	await settle()
	lock.close()
	for dialog: AcceptDialog in get_tree().root.find_children("*", "AcceptDialog", true, false):
		if dialog.visible:
			dialog.hide()
	if not is_instance_valid(scene):
		check(false, "failed save destroyed the edited scene")
		return
	check(scene.editor_description == "pending edit", "failed save preserved the edit")
	if action != "editor/reload_saved_scene":
		var tabs := find_scene_tabs()
		for index in tabs.tab_count:
			if tabs.get_tab_tooltip(index).ends_with(other.get_file()):
				tabs.tab_close_pressed.emit(index)
				break
		await settle()
		check(not EditorInterface.get_open_scenes().has(other), "the clean tab closes independently")
		check(EditorInterface.get_open_scenes().has(path), "the failed tab remains open")
	check(EditorInterface.save_scene_root(scene) == OK, "retry saves the edited scene")
	var fresh := (ResourceLoader.load(path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE_DEEP) as PackedScene).instantiate()
	check(fresh.editor_description == "pending edit", "reload from disk confirms retry")
	fresh.free()
