/**************************************************************************/
/*  animation_workbench_editor_plugin.cpp                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "animation_workbench_editor_plugin.h"

#include "core/io/dir_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/io/resource_uid.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/inspector/editor_resource_picker.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/scene/editor_scene_tabs.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/animation/animation_player.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/check_button.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/split_container.h"
#include "scene/gui/subviewport_container.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/tree.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/animation_library.h"
#include "scene/resources/environment.h"
#include "scene/resources/font.h"
#include "scene/resources/immediate_mesh.h"
#include "scene/resources/material.h"
#include "scene/resources/packed_scene.h"

void AnimationClipTimeline::_bind_methods() {
	ADD_SIGNAL(MethodInfo("drag_started"));
	ADD_SIGNAL(MethodInfo("selection_changed", PropertyInfo(Variant::FLOAT, "first"), PropertyInfo(Variant::FLOAT, "last")));
}

AnimationClipTimeline::AnimationClipTimeline() {
	set_name("AnimationClipTimeline");
	set_custom_minimum_size(Size2(0, 88 * EDSCALE));
	set_focus_mode(FOCUS_ALL);
	set_step(0);
	connect("value_changed", callable_mp((CanvasItem *)this, &CanvasItem::queue_redraw).unbind(1));
}

double AnimationClipTimeline::_time_at(double p_x) const {
	return CLAMP(view_start + (p_x - 12 * EDSCALE) / MAX(1.0, get_size().x - 24 * EDSCALE) * view_span, 0.0, duration);
}

double AnimationClipTimeline::_x_at(double p_time) const {
	return 12 * EDSCALE + (p_time - view_start) / view_span * MAX(1.0, get_size().x - 24 * EDSCALE);
}

double AnimationClipTimeline::snap(double p_time) const {
	if (p_time >= duration - 0.000001) {
		return duration;
	}
	return CLAMP(rate > 0 ? Math::round(p_time * rate) / rate : p_time, 0.0, duration);
}

void AnimationClipTimeline::fit() {
	view_start = 0;
	view_span = MAX(duration, 0.001);
	queue_redraw();
}

void AnimationClipTimeline::set_clip(const Ref<Animation> &p_clip, double p_rate) {
	duration = p_clip.is_valid() ? p_clip->get_length() : 0;
	rate = p_rate;
	marks.clear();
	if (p_clip.is_valid()) {
		HashSet<int> bins;
		for (int track = 0; track < p_clip->get_track_count(); track++) {
			for (int key = 0; key < p_clip->track_get_key_count(track); key++) {
				bins.insert(Math::round(p_clip->track_get_key_time(track, key) / MAX(duration, 0.001) * 512));
			}
		}
		for (int bin : bins) {
			marks.push_back(bin / 512.0 * duration);
		}
	}
	set_max(MAX(duration, 0.001));
	selection = Vector2();
	drag = NONE;
	fit();
}

void AnimationClipTimeline::set_selection(double p_first, double p_last) {
	selection = Vector2(CLAMP(MIN(p_first, p_last), 0.0, duration), CLAMP(MAX(p_first, p_last), 0.0, duration));
	queue_redraw();
}

void AnimationClipTimeline::_notification(int p_what) {
	if (p_what != NOTIFICATION_DRAW) {
		return;
	}
	const Ref<Font> font = get_theme_default_font();
	const int font_size = get_theme_default_font_size();
	const Color ink = get_theme_color(SNAME("font_color"), SNAME("Label"));
	const Color accent = get_theme_color(SNAME("accent_color"), SNAME("Editor"));
	Rect2 lane(12 * EDSCALE, 48 * EDSCALE, MAX(1.0, get_size().x - 24 * EDSCALE), 34 * EDSCALE);
	draw_rect(Rect2(Point2(), get_size()), Color(0.08, 0.09, 0.11, 0.55));
	draw_rect(lane, Color(0.28, 0.38, 0.48, 0.5));
	if (duration <= 0) {
		return;
	}
	const double factor = frames && rate > 0 ? rate : 1;
	const double desired = view_span * factor / MAX(1.0, lane.size.x / (65 * EDSCALE));
	const double base = Math::pow(10.0, Math::floor(Math::log(MAX(desired, 0.0001)) / Math::log(10.0)));
	double tick = base;
	for (double step : { 1.0, 2.0, 5.0, 10.0 }) {
		tick = base * step;
		if (tick >= desired) {
			break;
		}
	}
	if (frames) {
		tick = MAX(1.0, Math::ceil(tick));
	}
	for (double t = Math::ceil(view_start * factor / tick) * tick / factor; t <= MIN(duration, view_start + view_span) + 0.000001; t += tick / factor) {
		const double x = _x_at(t);
		draw_line(Vector2(x, 29 * EDSCALE), Vector2(x, 43 * EDSCALE), ink * Color(1, 1, 1, 0.6));
		String label = frames ? itos(Math::round(t * rate)) : String::num(t, tick < 0.01 ? 3 : (tick < 0.1 ? 2 : 1));
		const double width = font->get_string_size(label, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x;
		draw_string(font, Vector2(CLAMP(x - width / 2, 2.0, MAX(2.0, get_size().x - width - 2)), 22 * EDSCALE), label, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, ink);
	}
	for (double mark : marks) {
		const double x = _x_at(mark);
		if (x >= lane.position.x && x <= lane.get_end().x) {
			draw_line(Vector2(x, 56 * EDSCALE), Vector2(x, 74 * EDSCALE), Color(0.65, 0.75, 0.85, 0.35));
		}
	}
	if (editing && selection.y > selection.x) {
		const double left = CLAMP(_x_at(selection.x), double(lane.position.x), double(lane.get_end().x));
		const double right = CLAMP(_x_at(selection.y), double(lane.position.x), double(lane.get_end().x));
		draw_rect(Rect2(left, lane.position.y, right - left, lane.size.y), Color(0.95, 0.4, 0.2, 0.42));
		for (double edge : { double(selection.x), double(selection.y) }) {
			const double x = _x_at(edge);
			if (x >= lane.position.x && x <= lane.get_end().x) {
				draw_rect(Rect2(x - 3 * EDSCALE, lane.position.y, 6 * EDSCALE, lane.size.y), Color(1, 0.55, 0.25));
			}
		}
	}
	const double head = _x_at(get_value());
	if (head >= lane.position.x && head <= lane.get_end().x) {
		draw_line(Vector2(head, 27 * EDSCALE), Vector2(head, 84 * EDSCALE), accent, 2 * EDSCALE);
		Vector<Vector2> triangle = { Vector2(head - 5 * EDSCALE, 26 * EDSCALE), Vector2(head + 5 * EDSCALE, 26 * EDSCALE), Vector2(head, 33 * EDSCALE) };
		draw_colored_polygon(triangle, accent);
	}
}

void AnimationClipTimeline::_move(double p_x) {
	if (drag == PAN) {
		view_start = CLAMP(pan_start - (p_x - anchor) / MAX(1.0, get_size().x - 24 * EDSCALE) * view_span, 0.0, MAX(0.0, duration - view_span));
	} else {
		const double t = snap(_time_at(p_x));
		set_value(t);
		if (drag == SELECTION) {
			set_selection(anchor, t);
			emit_signal("selection_changed", selection.x, selection.y);
		}
	}
	queue_redraw();
}

void AnimationClipTimeline::gui_input(const Ref<InputEvent> &p_event) {
	if (duration <= 0) {
		return;
	}
	Ref<InputEventKey> key = p_event;
	if (key.is_valid() && key->is_pressed() && (key->get_keycode() == Key::LEFT || key->get_keycode() == Key::RIGHT)) {
		emit_signal("drag_started");
		set_value(snap(get_value() + (key->get_keycode() == Key::LEFT ? -1 : 1) / (rate > 0 ? rate : 1000.0)));
		accept_event();
	}
	Ref<InputEventMouseButton> button = p_event;
	if (button.is_valid()) {
		const MouseButton index = button->get_button_index();
		if (button->is_pressed() && (index == MouseButton::WHEEL_UP || index == MouseButton::WHEEL_DOWN)) {
			const double t = _time_at(button->get_position().x);
			const double fraction = (t - view_start) / view_span;
			view_span = CLAMP(view_span * (index == MouseButton::WHEEL_UP ? 0.8 : 1.25), MIN(duration, rate > 0 ? 4.0 / rate : 0.004), duration);
			view_start = CLAMP(t - fraction * view_span, 0.0, MAX(0.0, duration - view_span));
			queue_redraw();
			accept_event();
		} else if (index == MouseButton::MIDDLE) {
			drag = button->is_pressed() ? PAN : NONE;
			anchor = button->get_position().x;
			pan_start = view_start;
			accept_event();
		} else if (index == MouseButton::LEFT) {
			if (!button->is_pressed()) {
				drag = NONE;
				return;
			}
			grab_focus();
			emit_signal("drag_started");
			drag = button->get_position().y < 48 * EDSCALE || !editing ? HEAD : SELECTION;
			anchor = snap(_time_at(button->get_position().x));
			if (drag == SELECTION && selection.y > selection.x) {
				if (Math::abs(button->get_position().x - _x_at(selection.x)) < 8 * EDSCALE) {
					anchor = selection.y;
				} else if (Math::abs(button->get_position().x - _x_at(selection.y)) < 8 * EDSCALE) {
					anchor = selection.x;
				}
			}
			_move(button->get_position().x);
			accept_event();
		}
	}
	Ref<InputEventMouseMotion> motion = p_event;
	if (motion.is_valid() && drag != NONE) {
		_move(motion->get_position().x);
		accept_event();
	}
}

static Button *workbench_button(BoxContainer *p_parent, const String &p_text, const Callable &p_action) {
	Button *button = memnew(Button(p_text));
	p_parent->add_child(button);
	button->connect(SceneStringName(pressed), p_action);
	return button;
}

AnimationWorkbenchEditorPlugin::AnimationWorkbenchEditorPlugin() {
	workspace = memnew(VBoxContainer);
	workspace->set_name("AnimationAssetWorkspace");
	workspace->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	workspace->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	HBoxContainer *header = memnew(HBoxContainer);
	workspace->add_child(header);
	title = memnew(Label);
	title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	title->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	header->add_child(title);
	workbench_button(header, TTR("Import GLB..."), callable_mp(this, &AnimationWorkbenchEditorPlugin::_show_import).bind(String(), String("res://")));
	workbench_button(header, TTR("Focus Model"), callable_mp(this, &AnimationWorkbenchEditorPlugin::_frame_model));
	workbench_button(header, TTR("Refresh Animations"), callable_mp(this, &AnimationWorkbenchEditorPlugin::_refresh));
	HSplitContainer *split = memnew(HSplitContainer);
	split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	workspace->add_child(split);
	VSplitContainer *left = memnew(VSplitContainer);
	left->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	split->add_child(left);
	view = memnew(SubViewportContainer);
	view->set_stretch(true);
	view->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	view->set_custom_minimum_size(Size2(420, 280) * EDSCALE);
	view->set_focus_mode(Control::FOCUS_ALL);
	view->connect(SceneStringName(gui_input), callable_mp(this, &AnimationWorkbenchEditorPlugin::_preview_input));
	left->add_child(view);
	viewport = memnew(SubViewport);
	viewport->set_use_own_world_3d(true);
	viewport->set_handle_input_locally(false);
	view->add_child(viewport);
	world = memnew(Node3D);
	viewport->add_child(world);
	camera = memnew(Camera3D);
	camera->set_current(true);
	camera->set_near(0.01);
	world->add_child(camera);
	WorldEnvironment *environment = memnew(WorldEnvironment);
	Ref<Environment> settings;
	settings.instantiate();
	settings->set_background(Environment::BG_COLOR);
	settings->set_bg_color(Color(0.16, 0.18, 0.21));
	settings->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
	settings->set_ambient_light_color(Color(0.85, 0.87, 0.92));
	settings->set_ambient_light_energy(0.6);
	environment->set_environment(settings);
	world->add_child(environment);
	for (Vector3 angle : { Vector3(-45, -35, 0), Vector3(-20, 140, 0) }) {
		DirectionalLight3D *light = memnew(DirectionalLight3D);
		light->set_rotation_degrees(angle);
		light->set_param(Light3D::PARAM_ENERGY, 1.2);
		world->add_child(light);
	}
	Ref<ImmediateMesh> grid;
	grid.instantiate();
	Ref<StandardMaterial3D> material;
	material.instantiate();
	material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	material->set_albedo(Color(0.28, 0.3, 0.34));
	grid->surface_begin(Mesh::PRIMITIVE_LINES, material);
	for (int i = -10; i <= 10; i++) {
		grid->surface_add_vertex(Vector3(i, 0, -10));
		grid->surface_add_vertex(Vector3(i, 0, 10));
		grid->surface_add_vertex(Vector3(-10, 0, i));
		grid->surface_add_vertex(Vector3(10, 0, i));
	}
	grid->surface_end();
	MeshInstance3D *floor = memnew(MeshInstance3D);
	floor->set_mesh(grid);
	world->add_child(floor);
	VBoxContainer *controls = memnew(VBoxContainer);
	controls->set_custom_minimum_size(Size2(0, 210 * EDSCALE));
	left->add_child(controls);
	clip_title = memnew(Label);
	controls->add_child(clip_title);
	HBoxContainer *transport = memnew(HBoxContainer);
	controls->add_child(transport);
	play = memnew(Button);
	play->set_name("Playback");
	play->set_toggle_mode(true);
	play->connect(SceneStringName(toggled), callable_mp(this, &AnimationWorkbenchEditorPlugin::_play_toggled));
	transport->add_child(play);
	OptionButton *rate = memnew(OptionButton);
	for (const String &label : { String("0.25x"), String("0.5x"), String("1x"), String("2x") }) {
		rate->add_item(label);
	}
	rate->select(2);
	rate->connect(SceneStringName(item_selected), callable_mp(this, &AnimationWorkbenchEditorPlugin::_speed_selected));
	transport->add_child(rate);
	loop = memnew(Button);
	loop->set_toggle_mode(true);
	loop->set_pressed(true);
	loop->set_tooltip_text(TTR("Loop Preview"));
	transport->add_child(loop);
	units = memnew(OptionButton);
	units->add_item(TTR("Frames"));
	units->add_item(TTR("Seconds"));
	units->select(1);
	units->connect(SceneStringName(item_selected), callable_mp(this, &AnimationWorkbenchEditorPlugin::_units_changed));
	transport->add_child(units);
	CheckButton *editing = memnew(CheckButton(TTR("Clip Editing")));
	editing->set_pressed(true);
	transport->add_child(editing);
	transport->add_spacer();
	workbench_button(transport, TTR("Reset Pose"), callable_mp(this, &AnimationWorkbenchEditorPlugin::_reset_pose));
	timeline = memnew(AnimationClipTimeline);
	workbench_button(transport, TTR("Reset Timeline"), callable_mp(timeline, &AnimationClipTimeline::fit));
	clock = memnew(Label);
	controls->add_child(clock);
	controls->add_child(timeline);
	timeline->connect("drag_started", callable_mp(this, &AnimationWorkbenchEditorPlugin::_pause));
	timeline->connect("value_changed", callable_mp(this, &AnimationWorkbenchEditorPlugin::_sample));
	timeline->connect("selection_changed", callable_mp(this, &AnimationWorkbenchEditorPlugin::_set_selection));
	VBoxContainer *edit_controls = memnew(VBoxContainer);
	controls->add_child(edit_controls);
	HBoxContainer *range = memnew(HBoxContainer);
	edit_controls->add_child(range);
	range->add_child(memnew(Label(TTR("Selection Start"))));
	first = memnew(SpinBox);
	first->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	first->connect("value_changed", callable_mp(this, &AnimationWorkbenchEditorPlugin::_range_changed));
	range->add_child(first);
	range->add_child(memnew(Label(TTR("Selection End"))));
	last = memnew(SpinBox);
	last->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	last->connect("value_changed", callable_mp(this, &AnimationWorkbenchEditorPlugin::_range_changed));
	range->add_child(last);
	HBoxContainer *actions = memnew(HBoxContainer);
	edit_controls->add_child(actions);
	delete_button = workbench_button(actions, TTR("Delete Selection"), callable_mp(this, &AnimationWorkbenchEditorPlugin::_delete_selected));
	undo_button = workbench_button(actions, TTR("Undo"), callable_mp(this, &AnimationWorkbenchEditorPlugin::undo_edit));
	redo_button = workbench_button(actions, TTR("Redo"), callable_mp(this, &AnimationWorkbenchEditorPlugin::redo_edit));
	save_button = workbench_button(actions, TTR("Save Current Clip"), callable_mp(this, &AnimationWorkbenchEditorPlugin::save_animation));
	workbench_button(actions, TTR("Save Clip As..."), callable_mp(this, &AnimationWorkbenchEditorPlugin::_save_as_requested));
	editing->connect(SceneStringName(toggled), callable_mp((CanvasItem *)edit_controls, &CanvasItem::set_visible));
	editing->connect(SceneStringName(toggled), callable_mp(timeline, &AnimationClipTimeline::set_editing));
	status = memnew(Label);
	status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	controls->add_child(status);
	VBoxContainer *sidebar = memnew(VBoxContainer);
	sidebar->set_custom_minimum_size(Size2(290 * EDSCALE, 0));
	split->add_child(sidebar);
	sidebar->add_child(memnew(Label(TTR("Preview Model"))));
	model_picker = memnew(EditorResourcePicker);
	model_picker->set_base_type("PackedScene");
	model_picker->connect("resource_changed", callable_mp(this, &AnimationWorkbenchEditorPlugin::_set_model));
	sidebar->add_child(model_picker);
	search = memnew(LineEdit);
	search->set_name("ClipSearch");
	search->set_placeholder(TTR("Search skeleton animations"));
	search->set_clear_button_enabled(true);
	search->connect(SceneStringName(text_changed), callable_mp(this, &AnimationWorkbenchEditorPlugin::_filter));
	sidebar->add_child(search);
	list = memnew(Tree);
	list->set_name("ClipList");
	list->set_hide_root(true);
	list->set_columns(2);
	list->set_column_clip_content(0, true);
	list->set_column_expand(1, false);
	list->set_column_custom_minimum_width(1, 70 * EDSCALE);
	list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	list->connect("item_selected", callable_mp(this, &AnimationWorkbenchEditorPlugin::_list_selected));
	sidebar->add_child(list);
	navigation = memnew(ConfirmationDialog);
	navigation->set_title(TTR("Unsaved Animation"));
	navigation->set_text(TTR("Save the current animation before continuing?"));
	navigation->set_ok_button_text(TTR("Save"));
	navigation->add_button(TTR("Discard"), true, "discard");
	navigation->connect(SceneStringName(confirmed), callable_mp(this, &AnimationWorkbenchEditorPlugin::_navigation_save));
	navigation->connect("custom_action", callable_mp(this, &AnimationWorkbenchEditorPlugin::_navigation_discard));
	navigation->connect("canceled", callable_mp(this, &AnimationWorkbenchEditorPlugin::_navigation_cancel));
	add_child(navigation);
	save_as = memnew(EditorFileDialog);
	save_as->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
	save_as->add_filter("*.anim", TTR("Animation"));
	save_as->connect("file_selected", callable_mp(this, &AnimationWorkbenchEditorPlugin::_save_as_file));
	add_child(save_as);
	_build_import_dialog();
	EditorInterface *editor = EditorInterface::get_singleton();
	editor->set_3d_asset_workspace(workspace);
	editor->connect("animation_player_editor_requested", callable_mp(this, &AnimationWorkbenchEditorPlugin::open_player));
	editor->connect("editor_document_restore_requested", callable_mp(this, &AnimationWorkbenchEditorPlugin::_restore_document));
	editor->connect("editor_document_selected", callable_mp(this, &AnimationWorkbenchEditorPlugin::_document_selected));
	editor->connect("scene_document_selected", callable_mp(this, &AnimationWorkbenchEditorPlugin::_leave_document));
	editor->connect("animation_asset_files_dropped", callable_mp(this, &AnimationWorkbenchEditorPlugin::_files_dropped), CONNECT_DEFERRED);
	editor->connect("skeletal_mesh_update_requested", callable_mp(this, &AnimationWorkbenchEditorPlugin::_show_update), CONNECT_DEFERRED);
	set_process(true);
	set_process_input(true);
	_update_camera();
}

void AnimationWorkbenchEditorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("open_player", "player"), &AnimationWorkbenchEditorPlugin::open_player);
	ClassDB::bind_method(D_METHOD("open_resource", "resource"), &AnimationWorkbenchEditorPlugin::open_resource);
	ClassDB::bind_method(D_METHOD("select_animation", "name"), &AnimationWorkbenchEditorPlugin::select_animation);
	ClassDB::bind_method(D_METHOD("delete_range", "first", "last"), &AnimationWorkbenchEditorPlugin::delete_range);
	ClassDB::bind_method(D_METHOD("save_animation"), &AnimationWorkbenchEditorPlugin::save_animation);
	ClassDB::bind_method(D_METHOD("undo_edit"), &AnimationWorkbenchEditorPlugin::undo_edit);
	ClassDB::bind_method(D_METHOD("redo_edit"), &AnimationWorkbenchEditorPlugin::redo_edit);
	ClassDB::bind_method(D_METHOD("get_current_animation"), &AnimationWorkbenchEditorPlugin::get_current_animation);
}

bool AnimationWorkbenchEditorPlugin::handles(Object *p_object) const {
	return Object::cast_to<SkeletonDefinition>(p_object) || Object::cast_to<Animation>(p_object);
}

void AnimationWorkbenchEditorPlugin::edit(Object *p_object) {
	Ref<Resource> resource = Object::cast_to<Resource>(p_object);
	if (resource.is_valid()) {
		open_resource(resource);
	}
}

void AnimationWorkbenchEditorPlugin::_register_document(const String &p_path, const Document &p_document, bool p_activate) {
	if (!documents.has(p_path)) {
		documents.insert(p_path, p_document);
	}
	Node *owner = Object::cast_to<Node>(ObjectDB::get_instance(p_document.scene));
	const String owner_name = owner ? (owner->get_scene_file_path().is_empty() ? String(owner->get_name()) : owner->get_scene_file_path().get_file().get_basename()) : String();
	const String label = owner ? owner_name + " / AnimationPlayer" : p_path.get_file();
	const StringName icon = p_document.player.is_valid() ? SNAME("AnimationPlayer") : (Ref<Animation>(p_document.resource).is_valid() ? SNAME("Animation") : SNAME("Skeleton3D"));
	EditorSceneTabs::get_singleton()->open_document(p_path, label, workspace->get_theme_icon(icon, SNAME("EditorIcons")), callable_mp(this, &AnimationWorkbenchEditorPlugin::_activate_document).bind(p_path), callable_mp(this, &AnimationWorkbenchEditorPlugin::_save_document).bind(p_path), callable_mp(this, &AnimationWorkbenchEditorPlugin::_close_document).bind(p_path), callable_mp(this, &AnimationWorkbenchEditorPlugin::_navigate), callable_mp(this, &AnimationWorkbenchEditorPlugin::_save_as_requested), p_activate);
}

void AnimationWorkbenchEditorPlugin::open_resource(const Ref<Resource> &p_resource) {
	ERR_FAIL_COND(p_resource.is_null() || p_resource->get_path().is_empty());
	Document document;
	document.resource = p_resource;
	_register_document(p_resource->get_path(), document, true);
}

void AnimationWorkbenchEditorPlugin::open_player(AnimationPlayer *p_player) {
	ERR_FAIL_NULL(p_player);
	for (const Variant &value : EditorInterface::get_singleton()->get_open_scene_roots()) {
		Node *root = Object::cast_to<Node>(value);
		if (root == p_player || root->is_ancestor_of(p_player)) {
			Document document;
			document.player = p_player->get_instance_id();
			document.scene = root->get_instance_id();
			String path = root->get_scene_file_path().is_empty() ? "unsaved://" + itos(root->get_instance_id()) : root->get_scene_file_path();
			path += "::AnimationPlayer/" + String(root->get_path_to(p_player));
			_register_document(path, document, true);
			return;
		}
	}
}

void AnimationWorkbenchEditorPlugin::_restore_document(const String &p_path) {
	if (p_path.contains("::AnimationPlayer/")) {
		for (const Variant &value : EditorInterface::get_singleton()->get_open_scene_roots()) {
			Node *root = Object::cast_to<Node>(value);
			if (root->get_scene_file_path() == p_path.get_slice("::", 0)) {
				AnimationPlayer *player = Object::cast_to<AnimationPlayer>(root->get_node_or_null(NodePath(p_path.get_slice("::AnimationPlayer/", 1))));
				if (player) {
					Document document;
					document.player = player->get_instance_id();
					document.scene = root->get_instance_id();
					_register_document(p_path, document, false);
				}
				return;
			}
		}
	} else if (p_path.has_extension("sk") || p_path.has_extension("anim")) {
		Document document;
		document.resource = ResourceLoader::load(p_path);
		if (document.resource.is_valid()) {
			_register_document(p_path, document, false);
		}
	}
}

Ref<SkeletonDefinition> AnimationWorkbenchEditorPlugin::_model_skeleton(const Ref<PackedScene> &p_scene) {
	Node *instance = p_scene->instantiate();
	AnimationPlayer *player = Object::cast_to<AnimationPlayer>(instance->get_node_or_null(NodePath("AnimationPlayer")));
	Ref<SkeletonDefinition> definition = player ? player->get_skeleton_definition() : Ref<SkeletonDefinition>();
	memdelete(instance);
	return definition;
}

void AnimationWorkbenchEditorPlugin::_activate_document(const String &p_path) {
	callable_mp(this, &AnimationWorkbenchEditorPlugin::_inspect_document).call_deferred(p_path);
	if (current_document == p_path && model) {
		EditorInterface::get_singleton()->show_3d_asset_workspace(true);
		EditorDockManager::get_singleton()->force_focus_dock(Node3DEditor::get_singleton());
		return;
	}
	_clear_model();
	current_document = p_path;
	Document &document = documents[p_path];
	title->set_text(p_path.get_file());
	title->set_tooltip_text(p_path);
	if (document.player.is_valid()) {
		AnimationPlayer *player = Object::cast_to<AnimationPlayer>(ObjectDB::get_instance(document.player));
		if (!player) {
			EditorSceneTabs::get_singleton()->remove_document(p_path);
			_close_document(p_path);
			return;
		}
		skeleton = player->get_skeleton_definition();
		Node3D *visual = Object::cast_to<Node3D>(player->get_node_or_null(player->get_root_node()));
		if (visual) {
			Node3D *copy = Object::cast_to<Node3D>(visual->duplicate(0));
			AnimationPlayer *catalog = memnew(AnimationPlayer);
			catalog->set_name("AnimationCatalog");
			catalog->set_root_node(NodePath(".."));
			LocalVector<StringName> libraries;
			player->get_animation_library_list(&libraries);
			for (const StringName &name : libraries) {
				catalog->add_animation_library(name, player->get_animation_library(name));
			}
			copy->add_child(catalog);
			_build_preview(copy, catalog);
		}
		Node *root = Object::cast_to<Node>(ObjectDB::get_instance(document.scene));
		title->set_text(String(root->get_name()) + " / " + String(root->get_path_to(player)));
		model_picker->hide();
	} else {
		Ref<Animation> clip = document.resource;
		skeleton = Ref<SkeletonDefinition>(document.resource);
		if (clip.is_valid()) {
			skeleton = clip->get_skeleton();
		}
		model_picker->show();
		if (skeleton.is_valid() && !skeleton->get_preview_mesh().is_empty()) {
			Ref<PackedScene> mesh = ResourceLoader::load(skeleton->get_preview_mesh());
			if (mesh.is_valid()) {
				Node3D *copy = Object::cast_to<Node3D>(mesh->instantiate());
				AnimationPlayer *catalog = Object::cast_to<AnimationPlayer>(copy->get_node_or_null(NodePath("AnimationPlayer")));
				if (catalog && catalog->get_skeleton_definition() == skeleton) {
					_build_preview(copy, catalog);
					updating = true;
					model_picker->set_edited_resource(mesh);
					updating = false;
				} else {
					memdelete(copy);
				}
			}
		}
		if (clip.is_valid() && catalog_player) {
			document.animation = catalog_player->find_animation(clip);
			if (document.animation == StringName()) {
				Ref<AnimationLibrary> preview_library;
				preview_library.instantiate();
				StringName name = clip->get_path().get_file().get_basename();
				preview_library->add_animation(name, clip);
				catalog_player->add_animation_library(SNAME("Preview"), preview_library);
				document.animation = "Preview/" + String(name);
			}
		}
	}
	visible = true;
	EditorInterface::get_singleton()->show_3d_asset_workspace(true);
	EditorDockManager::get_singleton()->force_focus_dock(Node3DEditor::get_singleton());
	hide_bottom_panel();
	if (catalog_player) {
		_refresh();
	} else {
		status->set_text(TTR("Choose a preview model that uses this skeleton."));
	}
	view->call_deferred("grab_focus");
}

void AnimationWorkbenchEditorPlugin::_inspect_document(const String &p_path) {
	if (EditorSceneTabs::get_singleton()->get_active_document() != p_path || !documents.has(p_path)) {
		return;
	}
	AnimationPlayer *player = Object::cast_to<AnimationPlayer>(ObjectDB::get_instance(documents[p_path].player));
	if (player) {
		EditorNode::get_singleton()->push_item(player, "", true);
	}
}

void AnimationWorkbenchEditorPlugin::_build_preview(Node3D *p_model, AnimationPlayer *p_catalog) {
	model = p_model;
	catalog_player = p_catalog;
	Vector<Node *> nodes;
	nodes.push_back(model);
	while (!nodes.is_empty()) {
		Node *node = nodes[nodes.size() - 1];
		nodes.remove_at(nodes.size() - 1);
		if (AnimationMixer *mixer = Object::cast_to<AnimationMixer>(node)) {
			mixer->set_active(false);
		}
		if (Node3D *spatial = Object::cast_to<Node3D>(node)) {
			Pose pose;
			pose.node = spatial;
			pose.transform = spatial->get_transform();
			if (Skeleton3D *bones = Object::cast_to<Skeleton3D>(spatial)) {
				for (int i = 0; i < bones->get_bone_count(); i++) {
					pose.bones.push_back(bones->get_bone_pose(i));
				}
			}
			if (MeshInstance3D *mesh = Object::cast_to<MeshInstance3D>(spatial)) {
				for (int i = 0; i < mesh->get_blend_shape_count(); i++) {
					pose.shapes.push_back(mesh->get_blend_shape_value(i));
				}
			}
			poses.push_back(pose);
		}
		for (Node *child : node->iterate_children()) {
			nodes.push_back(child);
		}
	}
	sample_player = memnew(AnimationPlayer);
	sample_player->set_root_node(NodePath(".."));
	sample_player->set_callback_mode_process(AnimationMixer::ANIMATION_CALLBACK_MODE_PROCESS_MANUAL);
	sample_player->set_reset_on_save_enabled(false);
	model->add_child(sample_player);
	world->add_child(model);
	catalog_player->connect("animation_list_changed", callable_mp(this, &AnimationWorkbenchEditorPlugin::_catalog_changed), CONNECT_DEFERRED);
	_frame_model();
}

void AnimationWorkbenchEditorPlugin::_set_model(const Ref<Resource> &p_model) {
	if (updating || p_model.is_null() || skeleton.is_null()) {
		return;
	}
	Ref<PackedScene> packed = p_model;
	if (packed.is_null() || _model_skeleton(packed) != skeleton) {
		status->set_text(TTR("The preview model must use the current skeleton."));
		return;
	}
	_navigate(callable_mp(this, &AnimationWorkbenchEditorPlugin::_apply_model).bind(packed));
}

void AnimationWorkbenchEditorPlugin::_apply_model(const Ref<PackedScene> &p_model) {
	_clear_model();
	Node3D *copy = Object::cast_to<Node3D>(p_model->instantiate());
	_build_preview(copy, Object::cast_to<AnimationPlayer>(copy->get_node_or_null(NodePath("AnimationPlayer"))));
	skeleton->set_preview_mesh(p_model->get_path());
	ResourceSaver::save(skeleton);
	_refresh();
}

void AnimationWorkbenchEditorPlugin::_clear_model() {
	_pause();
	current_clip = ObjectID();
	poses.clear();
	if (model) {
		memdelete(model);
	}
	model = nullptr;
	catalog_player = nullptr;
	sample_player = nullptr;
	timeline->set_clip(Ref<Animation>(), 0);
	list->clear();
	clip_title->set_text(String());
	clock->set_text(String());
	status->set_text(String());
	_update_controls();
}

void AnimationWorkbenchEditorPlugin::_leave_document() {
	_clear_model();
	current_document = String();
	visible = false;
	EditorInterface::get_singleton()->show_3d_asset_workspace(false);
}

void AnimationWorkbenchEditorPlugin::_document_selected(const String &p_path) {
	if (!documents.has(p_path) && visible) {
		_leave_document();
	}
}

void AnimationWorkbenchEditorPlugin::_close_document(const String &p_path) {
	if (current_document == p_path) {
		_leave_document();
	}
	documents.erase(p_path);
}

void AnimationWorkbenchEditorPlugin::_scene_closed(const String &p_path) {
	Vector<String> paths;
	for (const KeyValue<String, Document> &entry : documents) {
		Node *root = Object::cast_to<Node>(ObjectDB::get_instance(entry.value.scene));
		if (root && root->get_scene_file_path() == p_path) {
			paths.push_back(entry.key);
		}
	}
	for (const String &path : paths) {
		EditorSceneTabs::get_singleton()->remove_document(path);
		_close_document(path);
	}
}

void AnimationWorkbenchEditorPlugin::_node_removed(Node *p_node) {
	for (const KeyValue<String, Document> &entry : documents) {
		if (entry.value.player == p_node->get_instance_id()) {
			callable_mp(this, &AnimationWorkbenchEditorPlugin::_prune_documents).call_deferred();
			return;
		}
	}
}

void AnimationWorkbenchEditorPlugin::_prune_documents() {
	Vector<String> paths;
	for (const KeyValue<String, Document> &entry : documents) {
		if (entry.value.player.is_null()) {
			continue;
		}
		Node *player = Object::cast_to<Node>(ObjectDB::get_instance(entry.value.player));
		Node *root = Object::cast_to<Node>(ObjectDB::get_instance(entry.value.scene));
		if (!player || !root || (player != root && !root->is_ancestor_of(player))) {
			paths.push_back(entry.key);
		}
	}
	for (const String &path : paths) {
		EditorSceneTabs::get_singleton()->remove_document(path);
		_close_document(path);
	}
}

void AnimationWorkbenchEditorPlugin::_frame_model() {
	if (!model) {
		return;
	}
	AABB combined;
	bool found = false;
	Vector<Node *> pending;
	pending.push_back(model);
	while (!pending.is_empty()) {
		Node *node = pending[pending.size() - 1];
		pending.remove_at(pending.size() - 1);
		MeshInstance3D *mesh = Object::cast_to<MeshInstance3D>(node);
		if (mesh && mesh->get_mesh().is_valid() && mesh->is_visible()) {
			AABB bounds = mesh->get_global_transform().xform(mesh->get_aabb());
			combined = found ? combined.merge(bounds) : bounds;
			found = true;
		}
		for (Node *child : node->iterate_children()) {
			pending.push_back(child);
		}
	}
	if (found) {
		center = combined.get_center();
		distance = MAX(combined.get_size().length() * 1.2, 0.5);
	}
	_update_camera();
}

void AnimationWorkbenchEditorPlugin::_update_camera() {
	Basis basis = Basis::from_euler(Vector3(orbit.y, orbit.x, 0));
	camera->set_position(center + basis.xform(Vector3(0, 0, distance)));
	if (camera->is_inside_tree()) {
		camera->look_at(center);
	}
}

void AnimationWorkbenchEditorPlugin::_preview_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> button = p_event;
	if (button.is_valid() && button->is_pressed()) {
		view->grab_focus();
		if (button->get_button_index() == MouseButton::WHEEL_UP) {
			distance = MAX(distance * 0.9, 0.05);
		} else if (button->get_button_index() == MouseButton::WHEEL_DOWN) {
			distance = MIN(distance * 1.1, 10000.0);
		}
		_update_camera();
	}
	Ref<InputEventMouseMotion> motion = p_event;
	if (motion.is_valid()) {
		if (motion->get_button_mask().has_flag(MouseButtonMask::LEFT)) {
			orbit -= motion->get_relative() * 0.008;
			orbit.y = CLAMP(orbit.y, -1.45f, 1.45f);
		} else if (motion->get_button_mask().has_flag(MouseButtonMask::MIDDLE)) {
			center += camera->get_basis().xform(Vector3(-motion->get_relative().x, motion->get_relative().y, 0)) * distance * 0.0015;
		}
		_update_camera();
	}
}

void AnimationWorkbenchEditorPlugin::_reset_pose() {
	_pause();
	for (const Pose &pose : poses) {
		pose.node->set_transform(pose.transform);
		if (Skeleton3D *bones = Object::cast_to<Skeleton3D>(pose.node)) {
			for (int i = 0; i < pose.bones.size(); i++) {
				bones->set_bone_pose(i, pose.bones[i]);
			}
		}
		if (MeshInstance3D *mesh = Object::cast_to<MeshInstance3D>(pose.node)) {
			for (int i = 0; i < pose.shapes.size(); i++) {
				mesh->set_blend_shape_value(i, pose.shapes[i]);
			}
		}
	}
}

void AnimationWorkbenchEditorPlugin::_filter(const String &p_query) {
	const String query = p_query.strip_edges().to_lower();
	updating = true;
	list->clear();
	TreeItem *root = list->create_item();
	if (catalog_player) {
		LocalVector<StringName> libraries;
		catalog_player->get_animation_library_list(&libraries);
		for (const StringName &library_name : libraries) {
			Ref<AnimationLibrary> library = catalog_player->get_animation_library(library_name);
			TreeItem *group = list->create_item(root);
			group->set_text(0, library_name == StringName() ? TTR("Skeleton Animations") : String(library_name));
			group->set_selectable(0, false);
			group->set_selectable(1, false);
			LocalVector<StringName> names;
			library->get_animation_list(&names);
			int count = 0;
			for (const StringName &name : names) {
				StringName full_name = library_name == StringName() ? name : StringName(String(library_name) + "/" + String(name));
				if (!query.is_empty() && !String(full_name).to_lower().contains(query)) {
					continue;
				}
				Ref<Animation> clip = library->get_animation(name);
				TreeItem *item = list->create_item(group);
				item->set_text(0, name);
				item->set_text(1, String::num(clip->get_length(), 2) + " s");
				item->set_metadata(0, full_name);
				item->set_tooltip_text(0, clip->get_path());
				if (clip->get_instance_id() == current_clip) {
					item->select(0);
				}
				count++;
			}
			group->set_text(1, itos(count));
			if (!count && !query.is_empty()) {
				memdelete(group);
			}
		}
	}
	updating = false;
}

void AnimationWorkbenchEditorPlugin::_list_selected() {
	TreeItem *item = list->get_selected();
	if (!updating && item && item->get_metadata(0).get_type() == Variant::STRING_NAME) {
		StringName name = item->get_metadata(0);
		// Selecting an animation rebuilds the list after Tree finishes the selection event.
		callable_mp(this, &AnimationWorkbenchEditorPlugin::_navigate).call_deferred(callable_mp(this, &AnimationWorkbenchEditorPlugin::_select).bind(name, true));
	}
}

void AnimationWorkbenchEditorPlugin::select_animation(const StringName &p_name) {
	_navigate(callable_mp(this, &AnimationWorkbenchEditorPlugin::_select).bind(p_name, false));
}

void AnimationWorkbenchEditorPlugin::_select(const StringName &p_name, bool p_play) {
	if (!catalog_player || !catalog_player->has_animation(p_name)) {
		return;
	}
	Ref<Animation> source = catalog_player->get_animation(p_name);
	current_clip = source->get_instance_id();
	if (!drafts.has(current_clip)) {
		Draft draft;
		draft.source = source;
		draft.states.push_back(source->duplicate(true));
		drafts.insert(current_clip, draft);
		source->connect_changed(callable_mp(this, &AnimationWorkbenchEditorPlugin::_source_changed).bind(current_clip));
	}
	drafts[current_clip].scene = documents[current_document].scene;
	documents[current_document].animation = p_name;
	_render_draft();
	_filter(search->get_text());
	if (p_play) {
		_play_toggled(true);
	}
}

void AnimationWorkbenchEditorPlugin::_refresh() {
	if (!catalog_player) {
		return;
	}
	LocalVector<StringName> names;
	catalog_player->get_animation_list(&names);
	if (names.is_empty()) {
		_filter(search->get_text());
		return;
	}
	StringName selected = documents[current_document].animation;
	if (!catalog_player->has_animation(selected)) {
		selected = names[0];
		for (const StringName &name : names) {
			if (String(name).get_file() != "RESET") {
				selected = name;
				break;
			}
		}
	}
	_select(selected, false);
}

Ref<Animation> AnimationWorkbenchEditorPlugin::get_current_animation() const {
	return drafts.has(current_clip) ? drafts[current_clip].current() : Ref<Animation>();
}

static bool workbench_pose_track(Animation::TrackType p_type) {
	return p_type == Animation::TYPE_POSITION_3D || p_type == Animation::TYPE_ROTATION_3D || p_type == Animation::TYPE_SCALE_3D || p_type == Animation::TYPE_BLEND_SHAPE;
}

void AnimationWorkbenchEditorPlugin::_render_draft() {
	_reset_pose();
	Draft &draft = drafts[current_clip];
	Ref<Animation> clip = draft.current();
	Ref<Animation> sample = clip->duplicate(true);
	sample->set_loop_mode(Animation::LOOP_NONE);
	for (int track = 0; track < sample->get_track_count(); track++) {
		const Animation::TrackType type = sample->track_get_type(track);
		NodePath path = sample->track_get_path(track);
		Node *node = model->get_node_or_null(NodePath(path.get_concatenated_names()));
		bool valid = workbench_pose_track(type) && node;
		if (Skeleton3D *bones = Object::cast_to<Skeleton3D>(node)) {
			valid = valid && path.get_subname_count() == 1 && bones->find_bone(path.get_subname(0)) >= 0;
		} else if (type == Animation::TYPE_BLEND_SHAPE) {
			MeshInstance3D *mesh = Object::cast_to<MeshInstance3D>(node);
			valid = mesh && path.get_subname_count() == 1 && mesh->find_blend_shape_by_name(path.get_subname(0)) >= 0;
		} else {
			valid = valid && Object::cast_to<Node3D>(node) && path.get_subname_count() == 0;
		}
		if (!valid) {
			sample->track_set_enabled(track, false);
		}
	}
	if (sample_player->has_animation_library(StringName())) {
		sample_player->remove_animation_library(StringName());
	}
	Ref<AnimationLibrary> library;
	library.instantiate();
	library->add_animation(SNAME("preview"), sample);
	sample_player->add_animation_library(StringName(), library);
	sample_player->set_assigned_animation(SNAME("preview"));
	double fps = clip->get_meta("_animation_sample_rate", 0.0);
	if (!Math::is_finite(fps) || fps <= 0) {
		fps = clip->get_step() > 0 ? 1.0 / clip->get_step() : 0;
	}
	units->set_item_disabled(0, fps <= 0);
	if (fps <= 0) {
		units->select(1);
	}
	timeline->set_clip(clip, fps);
	timeline->set_frames(units->get_selected() == 0);
	_set_selection(0, 0);
	_sample(0);
	clip_title->set_text(String(documents[current_document].animation) + (draft.dirty() ? " *" : ""));
	_update_controls();
	_update_document_status();
}

void AnimationWorkbenchEditorPlugin::_sample(double p_time) {
	if (updating || !sample_player || !drafts.has(current_clip)) {
		return;
	}
	time = CLAMP(p_time, 0.0, drafts[current_clip].current()->get_length());
	sample_player->seek(time, true, true);
	updating = true;
	timeline->set_value(time);
	updating = false;
	Ref<Animation> clip = drafts[current_clip].current();
	double fps = clip->get_meta("_animation_sample_rate", clip->get_step() > 0 ? 1.0 / clip->get_step() : 0);
	String text = String::num(time, 3) + " / " + String::num(clip->get_length(), 3) + " s";
	if (fps > 0) {
		text += "   " + itos(Math::round(time * fps)) + " / " + itos(Math::ceil(clip->get_length() * fps - 0.00001)) + " " + TTR("Frames");
	}
	clock->set_text(text);
}

void AnimationWorkbenchEditorPlugin::_pause() {
	playing = false;
	play->set_pressed_no_signal(false);
	if (workspace->is_inside_tree()) {
		play->set_button_icon(workspace->get_theme_icon(SNAME("Play"), SNAME("EditorIcons")));
	}
}

void AnimationWorkbenchEditorPlugin::_play_toggled(bool p_play) {
	if (!drafts.has(current_clip)) {
		_pause();
		return;
	}
	playing = p_play;
	play->set_pressed_no_signal(p_play);
	play->set_button_icon(workspace->get_theme_icon(p_play ? SNAME("Pause") : SNAME("Play"), SNAME("EditorIcons")));
	if (p_play && time >= drafts[current_clip].current()->get_length()) {
		_sample(0);
	}
}

void AnimationWorkbenchEditorPlugin::_set_selection(double p_first, double p_last) {
	timeline->set_selection(p_first, p_last);
	Ref<Animation> clip = get_current_animation();
	double fps = clip.is_valid() ? double(clip->get_meta("_animation_sample_rate", clip->get_step() > 0 ? 1.0 / clip->get_step() : 0)) : 0;
	const bool frames = units->get_selected() == 0 && fps > 0;
	const double factor = frames ? fps : 1;
	updating = true;
	for (SpinBox *box : { first, last }) {
		box->set_step(frames ? 1 : 0.001);
		box->set_max(clip.is_valid() ? Math::ceil(clip->get_length() * factor) : 0);
		box->set_suffix(frames ? TTR("Frames") : TTR("Seconds"));
	}
	first->set_value(p_first * factor);
	last->set_value(p_last * factor);
	updating = false;
	_update_controls();
}

void AnimationWorkbenchEditorPlugin::_range_changed(double p_value) {
	if (updating) {
		return;
	}
	Ref<Animation> clip = get_current_animation();
	const double fps = clip.is_valid() ? double(clip->get_meta("_animation_sample_rate", clip->get_step() > 0 ? 1.0 / clip->get_step() : 0)) : 0;
	const double factor = units->get_selected() == 0 && fps > 0 ? fps : 1;
	_set_selection(timeline->snap(first->get_value() / factor), timeline->snap(last->get_value() / factor));
}

void AnimationWorkbenchEditorPlugin::_units_changed(int p_index) {
	timeline->set_frames(p_index == 0);
	Vector2 selected = timeline->get_selection();
	_set_selection(selected.x, selected.y);
}

void AnimationWorkbenchEditorPlugin::_update_controls() {
	const Draft *draft = drafts.getptr(current_clip);
	play->set_disabled(!draft);
	delete_button->set_disabled(!draft || timeline->get_selection().y <= timeline->get_selection().x);
	undo_button->set_disabled(!draft || draft->cursor <= 0);
	redo_button->set_disabled(!draft || draft->cursor + 1 >= draft->states.size());
	save_button->set_disabled(!draft || !draft->dirty());
}

static void workbench_boundary(const Ref<Animation> &p_source, const Ref<Animation> &p_result, int p_track, double p_time, double p_sample) {
	switch (p_source->track_get_type(p_track)) {
		case Animation::TYPE_POSITION_3D:
			p_result->position_track_insert_key(p_track, p_time, p_source->position_track_interpolate(p_track, p_sample));
			break;
		case Animation::TYPE_ROTATION_3D:
			p_result->rotation_track_insert_key(p_track, p_time, p_source->rotation_track_interpolate(p_track, p_sample));
			break;
		case Animation::TYPE_SCALE_3D:
			p_result->scale_track_insert_key(p_track, p_time, p_source->scale_track_interpolate(p_track, p_sample));
			break;
		case Animation::TYPE_BLEND_SHAPE:
			p_result->blend_shape_track_insert_key(p_track, p_time, p_source->blend_shape_track_interpolate(p_track, p_sample));
			break;
		default:
			break;
	}
}

Error AnimationWorkbenchEditorPlugin::delete_range(double p_first, double p_last) {
	Ref<Animation> source = get_current_animation();
	if (source.is_null() || !Math::is_finite(p_first) || !Math::is_finite(p_last) || p_first < 0 || p_last <= p_first || p_last > source->get_length() + 0.000001 || p_last - p_first >= source->get_length() - 0.000001) {
		return ERR_INVALID_PARAMETER;
	}
	for (int track = 0; track < source->get_track_count(); track++) {
		if (source->track_is_compressed(track) || (source->track_is_enabled(track) && !workbench_pose_track(source->track_get_type(track)))) {
			status->set_text(TTR("Clip editing requires uncompressed 3D pose or blend shape tracks."));
			return ERR_UNAVAILABLE;
		}
	}
	Ref<Animation> result = source->duplicate(true);
	const double removed = p_last - p_first;
	result->set_length(source->get_length() - removed);
	for (int track = 0; track < source->get_track_count(); track++) {
		if (!source->track_is_enabled(track)) {
			continue;
		}
		for (int key = result->track_get_key_count(track) - 1; key >= 0; key--) {
			result->track_remove_key(track, key);
		}
		for (int key = 0; key < source->track_get_key_count(track); key++) {
			const double t = source->track_get_key_time(track, key);
			if (t < p_first) {
				result->track_insert_key(track, t, source->track_get_key_value(track, key), source->track_get_key_transition(track, key));
			} else if (t >= p_last && t <= source->get_length()) {
				result->track_insert_key(track, t - removed, source->track_get_key_value(track, key), source->track_get_key_transition(track, key));
			}
		}
		if (p_first > 0 && p_last < source->get_length()) {
			workbench_boundary(source, result, track, MAX(0.0, p_first - 0.00001), MAX(0.0, p_first - 0.00001));
		}
		workbench_boundary(source, result, track, p_first, p_last < source->get_length() ? p_last : p_first);
	}
	Draft &draft = drafts[current_clip];
	if (draft.saved > draft.cursor) {
		draft.saved = -1;
	}
	draft.states.resize(draft.cursor + 1);
	draft.states.push_back(result);
	draft.cursor++;
	_render_draft();
	return OK;
}

void AnimationWorkbenchEditorPlugin::undo_edit() {
	if (drafts.has(current_clip) && drafts[current_clip].cursor > 0) {
		drafts[current_clip].cursor--;
		_render_draft();
	}
}

void AnimationWorkbenchEditorPlugin::redo_edit() {
	if (drafts.has(current_clip) && drafts[current_clip].cursor + 1 < drafts[current_clip].states.size()) {
		drafts[current_clip].cursor++;
		_render_draft();
	}
}

Error AnimationWorkbenchEditorPlugin::_save_draft(Draft &p_draft) {
	if (!p_draft.dirty()) {
		return OK;
	}
	Ref<Animation> before = p_draft.source->duplicate(true);
	p_draft.writing = true;
	p_draft.source->copy_from(p_draft.current());
	Error error;
	if (!p_draft.source->is_built_in()) {
		error = ResourceSaver::save(p_draft.source);
	} else {
		Node *root = Object::cast_to<Node>(ObjectDB::get_instance(p_draft.scene));
		error = root ? EditorInterface::get_singleton()->save_scene_root(root) : ERR_DOES_NOT_EXIST;
	}
	if (error != OK) {
		p_draft.source->copy_from(before);
		p_draft.writing = false;
		status->set_text(vformat(TTR("Animation save failed: %s"), error_names[error]));
		return error;
	}
	p_draft.saved = p_draft.cursor;
	p_draft.source->emit_changed();
	p_draft.writing = false;
	return OK;
}

Error AnimationWorkbenchEditorPlugin::save_animation() {
	if (saving || !drafts.has(current_clip)) {
		return ERR_UNAVAILABLE;
	}
	saving = true;
	Error error = _save_draft(drafts[current_clip]);
	saving = false;
	if (error == OK) {
		_render_draft();
		status->set_text(TTR("Current clip saved."));
	}
	return error;
}

bool AnimationWorkbenchEditorPlugin::_owns_clip(const Document &p_document, const Ref<Animation> &p_clip) const {
	if (p_document.resource == p_clip) {
		return true;
	}
	Ref<SkeletonDefinition> definition = p_document.resource;
	if (definition.is_valid()) {
		Ref<AnimationLibrary> library = definition->get_animation_library();
		LocalVector<StringName> names;
		library->get_animation_list(&names);
		for (const StringName &name : names) {
			if (library->get_animation(name) == p_clip) {
				return true;
			}
		}
		return false;
	}
	AnimationPlayer *player = Object::cast_to<AnimationPlayer>(ObjectDB::get_instance(p_document.player));
	return player && player->find_animation(p_clip) != StringName();
}

void AnimationWorkbenchEditorPlugin::_update_document_status() {
	for (const KeyValue<String, Document> &entry : documents) {
		bool dirty = false;
		for (const KeyValue<ObjectID, Draft> &draft : drafts) {
			dirty = dirty || (draft.value.dirty() && _owns_clip(entry.value, draft.value.source));
		}
		EditorSceneTabs::get_singleton()->set_document_modified(entry.key, dirty);
	}
}

void AnimationWorkbenchEditorPlugin::_save_document(const String &p_path) {
	for (KeyValue<ObjectID, Draft> &draft : drafts) {
		if (draft.value.dirty() && _owns_clip(documents[p_path], draft.value.source) && _save_draft(draft.value) != OK) {
			return;
		}
	}
	if (current_document == p_path && drafts.has(current_clip)) {
		_render_draft();
	}
	_update_document_status();
	_update_controls();
}

void AnimationWorkbenchEditorPlugin::_catalog_changed() {
	if (catalog_player) {
		_filter(search->get_text());
	}
}

void AnimationWorkbenchEditorPlugin::_source_changed(ObjectID p_clip) {
	Draft *draft = drafts.getptr(p_clip);
	if (!draft || draft->writing || draft->dirty() || draft->refresh_queued) {
		return;
	}
	draft->refresh_queued = true;
	callable_mp(this, &AnimationWorkbenchEditorPlugin::_refresh_source).call_deferred(p_clip);
}

void AnimationWorkbenchEditorPlugin::_refresh_source(ObjectID p_clip) {
	Draft &draft = drafts[p_clip];
	draft.refresh_queued = false;
	if (draft.dirty()) {
		return;
	}
	draft.states.clear();
	draft.states.push_back(draft.source->duplicate(true));
	draft.cursor = draft.saved = 0;
	if (current_clip == p_clip && model) {
		_render_draft();
	}
}

String AnimationWorkbenchEditorPlugin::get_unsaved_status(const String &p_scene) const {
	for (const KeyValue<ObjectID, Draft> &draft : drafts) {
		Node *root = Object::cast_to<Node>(ObjectDB::get_instance(draft.value.scene));
		if (draft.value.dirty() && (p_scene.is_empty() || (root && root->get_scene_file_path() == p_scene))) {
			return TTR("Animation workbench has unsaved clip edits.");
		}
	}
	return String();
}

void AnimationWorkbenchEditorPlugin::save_external_data() {
	if (saving) {
		return;
	}
	saving = true;
	for (KeyValue<ObjectID, Draft> &draft : drafts) {
		_save_draft(draft.value);
	}
	saving = false;
	_update_document_status();
}

void AnimationWorkbenchEditorPlugin::_save_as_requested() {
	if (!drafts.has(current_clip)) {
		return;
	}
	const String path = drafts[current_clip].source->get_path();
	save_as->set_current_path(path.get_base_dir().path_join(path.get_file().get_basename() + "_edited.anim"));
	save_as->popup_file_dialog();
}

void AnimationWorkbenchEditorPlugin::_save_as_file(const String &p_path) {
	if (!drafts.has(current_clip) || skeleton.is_null() || FileAccess::exists(p_path)) {
		status->set_text(TTR("Choose a new clip filename and a skeleton."));
		return;
	}
	Ref<Animation> clip = drafts[current_clip].current()->duplicate(true);
	clip->set_skeleton(skeleton);
	Error error = ResourceSaver::save(clip, p_path);
	if (error != OK) {
		status->set_text(error_names[error]);
		return;
	}
	clip->set_path(p_path, true);
	Ref<AnimationLibrary> library = skeleton->get_animation_library();
	String name = p_path.get_file().get_basename();
	const String base = name;
	for (int i = 2; library->has_animation(name); i++) {
		name = base + "_" + itos(i);
	}
	library->add_animation(name, clip);
	error = ResourceSaver::save(skeleton);
	if (error != OK) {
		library->remove_animation(name);
		DirAccess::remove_file_or_error(p_path);
		status->set_text(error_names[error]);
		return;
	}
	Draft &old = drafts[current_clip];
	old.states.clear();
	old.states.push_back(old.source->duplicate(true));
	old.cursor = old.saved = 0;
	_select(name, false);
	EditorFileSystem::get_singleton()->scan();
}

void AnimationWorkbenchEditorPlugin::_navigate(const Callable &p_action) {
	_pause();
	if (drafts.has(current_clip) && drafts[current_clip].dirty()) {
		pending_navigation = p_action;
		navigation->popup_centered();
	} else {
		p_action.call();
	}
}

void AnimationWorkbenchEditorPlugin::_navigation_save() {
	if (save_animation() == OK) {
		_continue_navigation();
	}
}

void AnimationWorkbenchEditorPlugin::_navigation_discard(const String &p_action) {
	Draft &draft = drafts[current_clip];
	draft.states.clear();
	draft.states.push_back(draft.source->duplicate(true));
	draft.cursor = draft.saved = 0;
	_render_draft();
	navigation->hide();
	_continue_navigation();
}

void AnimationWorkbenchEditorPlugin::_continue_navigation() {
	Callable action = pending_navigation;
	pending_navigation = Callable();
	if (action.is_valid()) {
		action.call();
	}
}

void AnimationWorkbenchEditorPlugin::input(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> key = p_event;
	if (key.is_null() || !visible || !workspace->is_visible_in_tree() || !drafts.has(current_clip) || key->is_alt_pressed()) {
		return;
	}
	Control *focus = workspace->get_viewport()->gui_get_focus_owner();
	if (!focus || (!workspace->is_ancestor_of(focus) && focus != workspace) || Object::cast_to<LineEdit>(focus) || Object::cast_to<TextEdit>(focus) || workspace->get_window()->get_last_exclusive_window() != workspace->get_window()) {
		return;
	}
	const bool command = key->is_command_or_control_pressed();
	const bool history = command && (key->get_keycode() == Key::Z || key->get_keycode() == Key::Y);
	const bool playback = !command && !key->is_shift_pressed() && key->get_keycode() == Key::SPACE;
	if (!history && !playback) {
		return;
	}
	if (key->is_pressed() && !key->is_echo()) {
		if (history) {
			if (key->is_shift_pressed() || key->get_keycode() == Key::Y) {
				redo_edit();
			} else {
				undo_edit();
			}
		} else {
			_play_toggled(!playing);
		}
	}
	get_viewport()->set_input_as_handled();
}

void AnimationWorkbenchEditorPlugin::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		set_name("AnimationWorkbench");
		connect("scene_closed", callable_mp(this, &AnimationWorkbenchEditorPlugin::_scene_closed));
		get_tree()->connect("node_removed", callable_mp(this, &AnimationWorkbenchEditorPlugin::_node_removed));
		loop->set_button_icon(workspace->get_theme_icon(SNAME("Loop"), SNAME("EditorIcons")));
		_pause();
		_update_controls();
		add_tool_menu_item(TTR("Import Model / Animation..."), callable_mp(this, &AnimationWorkbenchEditorPlugin::_show_import).bind(String(), String("res://")));
	} else if (p_what == NOTIFICATION_PROCESS) {
		if (playing && visible && workspace->is_visible_in_tree() && drafts.has(current_clip)) {
			const double duration = drafts[current_clip].current()->get_length();
			double next = time + get_process_delta_time() * speed;
			if (next >= duration) {
				if (loop->is_pressed()) {
					next = Math::fmod(next, duration);
				} else {
					next = duration;
					_pause();
				}
			}
			_sample(next);
		} else if (playing && !workspace->is_visible_in_tree()) {
			_pause();
		}
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		_clear_model();
		EditorInterface::get_singleton()->set_3d_asset_workspace(nullptr);
		memdelete(workspace);
		workspace = nullptr;
		for (const KeyValue<String, Document> &entry : documents) {
			EditorSceneTabs::get_singleton()->remove_document(entry.key);
		}
	}
}

static LineEdit *workbench_path(VBoxContainer *p_parent, const String &p_label, const Callable &p_browse) {
	p_parent->add_child(memnew(Label(p_label)));
	HBoxContainer *row = memnew(HBoxContainer);
	p_parent->add_child(row);
	LineEdit *path = memnew(LineEdit);
	path->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	row->add_child(path);
	workbench_button(row, TTR("Browse"), p_browse);
	return path;
}

void AnimationWorkbenchEditorPlugin::_build_import_dialog() {
	import_dialog = memnew(ConfirmationDialog);
	import_dialog->set_title(TTR("Import Model / Animation"));
	import_dialog->set_ok_button_text(TTR("Import Assets"));
	import_dialog->set_hide_on_ok(false);
	VBoxContainer *layout = memnew(VBoxContainer);
	import_dialog->add_child(layout);
	source_path = workbench_path(layout, TTR("External GLB"), callable_mp(this, &AnimationWorkbenchEditorPlugin::_browse_source));
	destination_row = memnew(VBoxContainer);
	layout->add_child(destination_row);
	destination_path = workbench_path(destination_row, TTR("Project Destination"), callable_mp(this, &AnimationWorkbenchEditorPlugin::_browse_destination));
	target_row = memnew(VBoxContainer);
	layout->add_child(target_row);
	target_row->add_child(memnew(Label(TTR("Update SKMesh"))));
	update_path = memnew(LineEdit);
	update_path->set_editable(false);
	target_row->add_child(update_path);
	import_options = memnew(VBoxContainer);
	layout->add_child(import_options);
	HBoxContainer *content = memnew(HBoxContainer);
	import_options->add_child(content);
	import_model = memnew(CheckBox(TTR("Import Model")));
	import_animations = memnew(CheckBox(TTR("Import Animations")));
	content->add_child(import_model);
	content->add_child(import_animations);
	import_model->set_pressed(true);
	import_animations->set_pressed(true);
	import_model->connect(SceneStringName(toggled), callable_mp(this, &AnimationWorkbenchEditorPlugin::_import_settings_changed));
	import_animations->connect(SceneStringName(toggled), callable_mp(this, &AnimationWorkbenchEditorPlugin::_import_settings_changed));
	HBoxContainer *bones = memnew(HBoxContainer);
	import_options->add_child(bones);
	Ref<ButtonGroup> group;
	group.instantiate();
	new_skeleton = memnew(CheckBox(TTR("Create Skeleton")));
	existing_skeleton = memnew(CheckBox(TTR("Use Existing Skeleton")));
	new_skeleton->set_button_group(group);
	existing_skeleton->set_button_group(group);
	bones->add_child(new_skeleton);
	bones->add_child(existing_skeleton);
	new_skeleton->set_pressed(true);
	new_skeleton->connect(SceneStringName(toggled), callable_mp(this, &AnimationWorkbenchEditorPlugin::_import_settings_changed));
	existing_skeleton->connect(SceneStringName(toggled), callable_mp(this, &AnimationWorkbenchEditorPlugin::_import_settings_changed));
	choose_skeleton = workbench_button(bones, TTR("Select"), callable_mp(this, &AnimationWorkbenchEditorPlugin::_browse_skeleton));
	skeleton_name = memnew(Label);
	bones->add_child(skeleton_name);
	HBoxContainer *fps = memnew(HBoxContainer);
	import_options->add_child(fps);
	fps->add_child(memnew(Label(TTR("Import Sampling FPS"))));
	sampling = memnew(SpinBox);
	sampling->set_min(1);
	sampling->set_max(240);
	sampling->set_value(30);
	fps->add_child(sampling);
	import_status = memnew(Label);
	import_status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	layout->add_child(import_status);
	import_dialog->connect(SceneStringName(confirmed), callable_mp(this, &AnimationWorkbenchEditorPlugin::_import_confirmed));
	import_dialog->connect("canceled", callable_mp(this, &AnimationWorkbenchEditorPlugin::_cancel_import));
	add_child(import_dialog);
	source_files = memnew(EditorFileDialog);
	source_files->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	source_files->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	source_files->add_filter("*.glb", "GLB");
	source_files->connect("file_selected", callable_mp(source_path, &LineEdit::set_text));
	import_dialog->add_child(source_files);
	target_folder = memnew(EditorFileDialog);
	target_folder->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_DIR);
	target_folder->connect("dir_selected", callable_mp(destination_path, &LineEdit::set_text));
	import_dialog->add_child(target_folder);
}

void AnimationWorkbenchEditorPlugin::_show_import(const String &p_source, const String &p_destination) {
	_navigate(callable_mp(this, &AnimationWorkbenchEditorPlugin::_open_import).bind(p_source, p_destination));
}

void AnimationWorkbenchEditorPlugin::_open_import(const String &p_source, const String &p_destination) {
	updating_mesh = false;
	import_dialog->set_title(TTR("Import Model / Animation"));
	import_dialog->set_ok_button_text(TTR("Import Assets"));
	source_path->set_text(p_source);
	destination_path->set_text(p_destination);
	destination_row->show();
	target_row->hide();
	import_options->show();
	import_status->set_text(String());
	_import_settings_changed(false);
	import_dialog->popup_centered(Size2i(700, 0) * EDSCALE);
}

void AnimationWorkbenchEditorPlugin::_show_update(const String &p_path) {
	_navigate(callable_mp(this, &AnimationWorkbenchEditorPlugin::_open_update).bind(p_path));
}

void AnimationWorkbenchEditorPlugin::_open_update(const String &p_path) {
	updating_mesh = true;
	import_dialog->set_title(TTR("Update SKMesh"));
	import_dialog->set_ok_button_text(TTR("Update SKMesh"));
	source_path->clear();
	update_path->set_text(p_path);
	destination_row->hide();
	target_row->show();
	import_options->hide();
	import_status->set_text(String());
	import_dialog->popup_centered(Size2i(700, 0) * EDSCALE);
}

void AnimationWorkbenchEditorPlugin::_import_settings_changed(bool p_pressed) {
	const bool only_animations = !import_model->is_pressed() && import_animations->is_pressed();
	new_skeleton->set_disabled(only_animations);
	if (only_animations) {
		existing_skeleton->set_pressed(true);
	}
	choose_skeleton->set_disabled(!existing_skeleton->is_pressed());
	skeleton_name->set_text(existing_skeleton->is_pressed() && import_skeleton.is_valid() ? import_skeleton->get_path().get_file() : String());
	sampling->set_editable(import_animations->is_pressed());
	import_dialog->get_ok_button()->set_disabled(!import_model->is_pressed() && !import_animations->is_pressed());
}

void AnimationWorkbenchEditorPlugin::_browse_skeleton() {
	import_dialog->hide();
	TypedArray<StringName> types;
	types.push_back("SkeletonDefinition");
	EditorInterface::get_singleton()->popup_quick_open(callable_mp(this, &AnimationWorkbenchEditorPlugin::_skeleton_chosen), types);
}

void AnimationWorkbenchEditorPlugin::_skeleton_chosen(const String &p_path) {
	if (!p_path.is_empty()) {
		import_skeleton = ResourceLoader::load(p_path);
	}
	_import_settings_changed(false);
	import_dialog->call_deferred("popup_centered");
}

void AnimationWorkbenchEditorPlugin::_browse_source() {
	if (!source_path->get_text().is_empty()) {
		source_files->set_current_path(source_path->get_text());
	}
	source_files->popup_file_dialog();
}

void AnimationWorkbenchEditorPlugin::_browse_destination() {
	target_folder->set_current_dir(destination_path->get_text());
	target_folder->popup_file_dialog();
}

void AnimationWorkbenchEditorPlugin::_import_confirmed() {
	Dictionary result;
	if (updating_mesh) {
		result = EditorInterface::get_singleton()->update_skeletal_mesh(source_path->get_text(), update_path->get_text());
	} else {
		if ((!new_skeleton->is_pressed() && !existing_skeleton->is_pressed()) || (existing_skeleton->is_pressed() && import_skeleton.is_null())) {
			import_status->set_text(TTR("Choose an existing skeleton or create a new skeleton."));
			return;
		}
		Dictionary settings;
		settings["animation_only"] = !import_model->is_pressed();
		settings["import_animations"] = import_animations->is_pressed();
		settings["skeleton"] = existing_skeleton->is_pressed() ? import_skeleton->get_path() : String();
		settings["fps"] = sampling->get_value();
		result = EditorInterface::get_singleton()->import_animation_assets(source_path->get_text(), destination_path->get_text(), settings);
	}
	if (int(result.get("error", FAILED)) != OK) {
		import_status->set_text(result.get("message", TTR("Import failed.")));
		return;
	}
	import_dialog->hide();
	const Dictionary import_metadata = result.get("metadata", Dictionary());
	const Array reports = import_metadata.get("skeleton_validation", Array());
	bool opened = false;
	for (const Variant &value : reports) {
		const Dictionary report = value;
		const String mesh = report.get("model", String());
		if (!mesh.is_empty()) {
			if (EditorInterface::get_singleton()->get_open_scenes().has(mesh)) {
				EditorInterface::get_singleton()->reload_scene_from_path(mesh);
			}
			EditorInterface::get_singleton()->open_scene_from_path(mesh);
			opened = true;
			break;
		}
		const PackedStringArray clips = report.get("animations", PackedStringArray());
		if (!clips.is_empty()) {
			open_resource(ResourceLoader::load(clips[0]));
			opened = true;
			break;
		}
	}
	if (!opened && import_metadata.has("scene")) {
		EditorInterface::get_singleton()->open_scene_from_path(import_metadata["scene"]);
	}
	callable_mp(this, &AnimationWorkbenchEditorPlugin::_next_drop).call_deferred();
}

void AnimationWorkbenchEditorPlugin::_files_dropped(const PackedStringArray &p_files, const String &p_folder) {
	for (const String &path : p_files) {
		dropped_files.push_back(Pair<String, String>(path, p_folder));
	}
	_next_drop();
}

void AnimationWorkbenchEditorPlugin::_next_drop() {
	if (dropped_files.is_empty() || import_dialog->is_visible() || navigation->is_visible()) {
		return;
	}
	const Pair<String, String> next = dropped_files[0];
	dropped_files.remove_at(0);
	_show_import(next.first, next.second);
}

void AnimationWorkbenchEditorPlugin::_cancel_import() {
	dropped_files.clear();
}

void AnimationWorkbenchEditorPlugin::_speed_selected(int p_index) {
	static const double speeds[] = { 0.25, 0.5, 1.0, 2.0 };
	speed = speeds[p_index];
}

void AnimationWorkbenchEditorPlugin::_delete_selected() {
	Vector2 selected = timeline->get_selection();
	delete_range(selected.x, selected.y);
}

void AnimationWorkbenchEditorPlugin::_navigation_cancel() {
	pending_navigation = Callable();
	dropped_files.clear();
}
