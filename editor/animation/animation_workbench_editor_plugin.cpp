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

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/io/resource_uid.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/gui/editor_file_dialog.h"
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
#include "scene/gui/dialogs.h"
#include "scene/gui/flow_container.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/option_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/popup_menu.h"
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
#include "scene/resources/material.h"
#include "scene/resources/packed_scene.h"
#include "scene/resources/immediate_mesh.h"
#include "scene/resources/style_box.h"

#include <cfloat>

AnimationWorkbenchEditorPlugin *AnimationWorkbenchEditorPlugin::singleton = nullptr;

static constexpr int WORKBENCH_SECONDS_DECIMALS = 3;
static constexpr double WORKBENCH_SECONDS_STEP = 0.001;

static String workbench_seconds(double p_seconds) {
	return String::num(p_seconds, WORKBENCH_SECONDS_DECIMALS).pad_decimals(WORKBENCH_SECONDS_DECIMALS);
}

static bool animation_resources_match(const Ref<Animation> &p_a, const Ref<Animation> &p_b) {
	if (p_a == p_b) {
		return p_a.is_valid();
	}
	if (p_a.is_null() || p_b.is_null()) {
		return false;
	}
	const String path_a = p_a->get_path();
	return !path_a.is_empty() && path_a == p_b->get_path();
}

void AnimationClipTimeline::_bind_methods() {
	ADD_SIGNAL(MethodInfo("drag_started"));
	ADD_SIGNAL(MethodInfo("selection_changed", PropertyInfo(Variant::FLOAT, "first"), PropertyInfo(Variant::FLOAT, "last")));
	ADD_SIGNAL(MethodInfo("selection_cleared"));
}

AnimationClipTimeline::AnimationClipTimeline() {
	set_name("AnimationClipTimeline");
	set_custom_minimum_size(Size2(0, 84 * EDSCALE));
	set_tooltip_text(TTR("Drag to scrub. Drag in the bottom strip to select a range. Right-click to clear the selection."));
	set_focus_mode(FOCUS_ALL);
	set_step(0);
	connect("value_changed", callable_mp((CanvasItem *)this, &CanvasItem::queue_redraw).unbind(1));
}

Rect2 AnimationClipTimeline::_get_selection_lane() const {
	return Rect2(12 * EDSCALE, 68 * EDSCALE, MAX(1.0, get_size().x - 24 * EDSCALE), 12 * EDSCALE);
}

Control::CursorShape AnimationClipTimeline::get_cursor_shape(const Point2 &p_pos) const {
	if (duration > 0 && (drag == SELECTION || (drag == NONE && _get_selection_lane().has_point(p_pos)))) {
		return CURSOR_CROSS;
	}
	return Range::get_cursor_shape(p_pos);
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

double AnimationClipTimeline::_selection_frame_at(double p_time) const {
	return CLAMP(Math::round(p_time * rate), 0.0, double(get_last_frame()));
}

double AnimationClipTimeline::snap_selection(double p_time) const {
	return rate > 0 ? _selection_frame_at(p_time) / rate : CLAMP(p_time, 0.0, duration);
}

void AnimationClipTimeline::fit() {
	view_start = 0;
	view_span = MAX(duration, 0.001);
	queue_redraw();
}

void AnimationClipTimeline::set_clip(const Ref<Animation> &p_clip, double p_rate) {
	duration = p_clip.is_valid() ? p_clip->get_length() : 0;
	rate = p_rate;
	frames = frames && rate > 0;
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
	_clear_selection();
	drag = NONE;
	fit();
}

void AnimationClipTimeline::_clear_selection() {
	selection_start = 0;
	selection_end = 0;
	selection_active = false;
	if (drag == SELECTION) {
		drag = NONE;
	}
	queue_redraw();
}

void AnimationClipTimeline::set_selection(double p_first, double p_last) {
	selection_start = CLAMP(MIN(p_first, p_last), 0.0, duration);
	selection_end = CLAMP(MAX(p_first, p_last), 0.0, duration);
	selection_active = duration > 0;
	queue_redraw();
}

bool AnimationClipTimeline::has_selection() const {
	return selection_active && (rate > 0 || selection_end > selection_start);
}

int64_t AnimationClipTimeline::get_selection_frame_count() const {
	if (!has_selection() || rate <= 0) {
		return 0;
	}
	return int64_t(_selection_frame_at(selection_end) - _selection_frame_at(selection_start)) + 1;
}

int64_t AnimationClipTimeline::get_last_frame() const {
	return MAX(int64_t(0), int64_t(Math::ceil(duration * rate - 0.00001)) - 1);
}

double AnimationClipTimeline::get_cut_start() const {
	return snap_selection(selection_start);
}

double AnimationClipTimeline::get_cut_end() const {
	if (rate <= 0) {
		return selection_end;
	}
	const double last_frame = _selection_frame_at(selection_end);
	return last_frame >= get_last_frame() ? duration : (last_frame + 1) / rate;
}

void AnimationClipTimeline::set_frames(bool p_frames) {
	frames = p_frames && rate > 0;
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
	const Rect2 selection_lane = _get_selection_lane();
	const Rect2 lane(selection_lane.position.x, 46 * EDSCALE, selection_lane.size.x, 20 * EDSCALE);
	draw_rect(Rect2(Point2(), get_size()), Color(0.08, 0.09, 0.11, 0.55));
	draw_rect(lane, Color(0.28, 0.38, 0.48, 0.5));
	if (duration <= 0) {
		return;
	}
	draw_rect(selection_lane, Color(0.64, 0.44, 0.28, 0.24));
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
	} else {
		tick = MAX(WORKBENCH_SECONDS_STEP, tick);
	}
	const double ruler_end = frames ? get_last_frame() / rate : duration;
	for (double t = Math::ceil(view_start * factor / tick) * tick / factor; t <= MIN(ruler_end, view_start + view_span) + 0.000001; t += tick / factor) {
		const double x = _x_at(t);
		draw_line(Vector2(x, 29 * EDSCALE), Vector2(x, 43 * EDSCALE), ink * Color(1, 1, 1, 0.6));
		const String label = frames ? itos(Math::round(t * rate)) : workbench_seconds(t);
		const double width = font->get_string_size(label, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x;
		draw_string(font, Vector2(CLAMP(x - width / 2, 2.0, MAX(2.0, get_size().x - width - 2)), 22 * EDSCALE), label, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, ink);
	}
	for (double mark : marks) {
		const double x = _x_at(mark);
		if (x >= lane.position.x && x <= lane.get_end().x) {
			draw_line(Vector2(x, 49 * EDSCALE), Vector2(x, 63 * EDSCALE), Color(0.65, 0.75, 0.85, 0.35));
		}
	}
	if (has_selection()) {
		const double left = CLAMP(_x_at(get_cut_start()), double(selection_lane.position.x), double(selection_lane.get_end().x));
		const double right = CLAMP(_x_at(get_cut_end()), double(selection_lane.position.x), double(selection_lane.get_end().x));
		draw_rect(Rect2(left, selection_lane.position.y, right - left, selection_lane.size.y), Color(0.95, 0.4, 0.2, 0.42));
		for (double edge : { get_cut_start(), snap_selection(selection_end) }) {
			const double x = _x_at(edge);
			if (x >= lane.position.x && x <= lane.get_end().x) {
				const Rect2 handle(x - 3 * EDSCALE, selection_lane.position.y, 6 * EDSCALE, selection_lane.size.y);
				draw_rect(handle.intersection(selection_lane), Color(1, 0.55, 0.25));
			}
		}
	}
	const double head = _x_at(get_value());
	if (head >= lane.position.x && head <= lane.get_end().x) {
		draw_line(Vector2(head, 27 * EDSCALE), Vector2(head, selection_lane.get_end().y), accent, 2 * EDSCALE);
		Vector<Vector2> triangle = { Vector2(head - 5 * EDSCALE, 26 * EDSCALE), Vector2(head + 5 * EDSCALE, 26 * EDSCALE), Vector2(head, 33 * EDSCALE) };
		draw_colored_polygon(triangle, accent);
	}
}

void AnimationClipTimeline::_move(double p_x) {
	if (drag == PAN) {
		view_start = CLAMP(pan_start - (p_x - anchor) / MAX(1.0, get_size().x - 24 * EDSCALE) * view_span, 0.0, MAX(0.0, duration - view_span));
	} else {
		const double pointer_time = _time_at(p_x);
		const double t = drag == SELECTION ? snap_selection(pointer_time) : snap(pointer_time);
		set_value(t);
		if (drag == SELECTION) {
			emit_signal("selection_changed", MIN(anchor, t), MAX(anchor, t));
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
		if (selection_active && button->is_pressed() && index == MouseButton::RIGHT) {
			_clear_selection();
			emit_signal("selection_cleared");
			accept_event();
		} else if (button->is_pressed() && (index == MouseButton::WHEEL_UP || index == MouseButton::WHEEL_DOWN)) {
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
			drag = _get_selection_lane().has_point(button->get_position()) ? SELECTION : HEAD;
			anchor = snap_selection(_time_at(button->get_position().x));
			if (drag == SELECTION && selection_end > selection_start) {
				const double start_distance = Math::abs(button->get_position().x - _x_at(get_cut_start()));
				const double end_distance = Math::abs(button->get_position().x - _x_at(snap_selection(selection_end)));
				if (MIN(start_distance, end_distance) < 8 * EDSCALE) {
					anchor = start_distance <= end_distance ? selection_end : selection_start;
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

static HBoxContainer *workbench_asset_card(VBoxContainer *p_parent, const String &p_title, PanelContainer *&r_card, Label *&r_value) {
	r_card = memnew(PanelContainer);
	r_card->set_theme_type_variation(SNAME("PanelContainerButtonGroup"));
	p_parent->add_child(r_card);
	VBoxContainer *content = memnew(VBoxContainer);
	r_card->add_child(content);
	content->add_child(memnew(Label(p_title)));
	HBoxContainer *row = memnew(HBoxContainer);
	content->add_child(row);
	r_value = memnew(Label);
	r_value->set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);
	r_value->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	r_value->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	r_value->set_mouse_filter(Control::MOUSE_FILTER_PASS);
	row->add_child(r_value);
	return row;
}

static void workbench_update_asset_action_theme(HBoxContainer *p_row) {
	const Color text_color = p_row->get_theme_color(SceneStringName(font_color), SNAME("Button"));
	const Color disabled_color = p_row->get_theme_color(SNAME("font_disabled_color"), SNAME("Button"));
	const Color action_color = text_color.lerp(disabled_color, 0.35f);
	const int font_size = Math::round(p_row->get_theme_default_font_size() * 0.85f);
	for (Node *child : p_row->iterate_children()) {
		if (Button *action = Object::cast_to<Button>(child)) {
			action->add_theme_color_override(SceneStringName(font_color), action_color);
			action->add_theme_font_size_override(SceneStringName(font_size), font_size);
		}
	}
}

static void workbench_style_asset_actions(HBoxContainer *p_row) {
	for (Node *child : p_row->iterate_children()) {
		if (Button *action = Object::cast_to<Button>(child)) {
			action->set_theme_type_variation(Object::cast_to<MenuButton>(action) ? SNAME("FlatMenuButton") : SceneStringName(FlatButton));
			action->set_flat(false);
			action->set_focus_mode(Control::FOCUS_ALL);
			action->set_default_cursor_shape(Control::CURSOR_POINTING_HAND);
		}
	}
	p_row->connect(SceneStringName(theme_changed), callable_mp_static(&workbench_update_asset_action_theme).bind(p_row));
	workbench_update_asset_action_theme(p_row);
}

static void workbench_locate_path(const String &p_path) {
	FileSystemDock *dock = EditorInterface::get_singleton()->get_file_system_dock();
	dock->make_visible();
	dock->select_file(p_path);
}

AnimationWorkbenchEditorPlugin::AnimationWorkbenchEditorPlugin() {
	singleton = this;
	workspace = memnew(VBoxContainer);
	workspace->set_name("AnimationAssetWorkspace");
	workspace->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	workspace->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	HBoxContainer *header = memnew(HBoxContainer);
	workspace->add_child(header);
	title = memnew(Label);
	title->set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);
	title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	title->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	header->add_child(title);
	workbench_button(header, TTR("Import GLB..."), callable_mp(this, &AnimationWorkbenchEditorPlugin::_show_import).bind(String(), String("res://")));
	workbench_button(header, TTR("Focus Model"), callable_mp(this, &AnimationWorkbenchEditorPlugin::_frame_model));
	workbench_button(header, TTR("Refresh Animations"), callable_mp(this, &AnimationWorkbenchEditorPlugin::_rescan));
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
	// World axes at the origin: X red, Y green, Z blue, one unit long on the positive side.
	Ref<StandardMaterial3D> axis_material;
	axis_material.instantiate();
	axis_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	axis_material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	grid->surface_begin(Mesh::PRIMITIVE_LINES, axis_material);
	const Vector3 axis_directions[3] = { Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) };
	const Color axis_colors[3] = { Color(0.96, 0.2, 0.32), Color(0.53, 0.84, 0.01), Color(0.16, 0.55, 0.96) };
	for (int axis = 0; axis < 3; axis++) {
		grid->surface_set_color(axis_colors[axis]);
		grid->surface_add_vertex(Vector3());
		grid->surface_set_color(axis_colors[axis]);
		grid->surface_add_vertex(axis_directions[axis]);
	}
	grid->surface_end();
	floor->set_mesh(grid);
	world->add_child(floor);
	axis_gizmo = memnew(Control);
	axis_gizmo->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	axis_gizmo->set_anchors_and_offsets_preset(Control::PRESET_TOP_RIGHT);
	axis_gizmo->set_offset(SIDE_LEFT, -96 * EDSCALE);
	axis_gizmo->set_offset(SIDE_TOP, 8 * EDSCALE);
	axis_gizmo->set_offset(SIDE_RIGHT, -8 * EDSCALE);
	axis_gizmo->set_offset(SIDE_BOTTOM, 96 * EDSCALE);
	axis_gizmo->connect(SceneStringName(draw), callable_mp(this, &AnimationWorkbenchEditorPlugin::_draw_axis_gizmo));
	view->add_child(axis_gizmo);
	VBoxContainer *controls = memnew(VBoxContainer);
	left->add_child(controls);
	HBoxContainer *clip_header = memnew(HBoxContainer);
	clip_header->add_theme_constant_override(SNAME("separation"), 12 * EDSCALE);
	controls->add_child(clip_header);
	clip_title = memnew(Label);
	clip_title->set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);
	clip_title->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS_FORCE);
	clip_title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	clip_title->set_mouse_filter(Control::MOUSE_FILTER_PASS);
	clip_header->add_child(clip_title);
	HBoxContainer *clip_modes = memnew(HBoxContainer);
	clip_header->add_child(clip_modes);
	Ref<ButtonGroup> clip_mode_group;
	clip_mode_group.instantiate();
	const String clip_mode_names[] = { TTR("Once", "Clip Loop Mode"), TTR("Loop", "Clip Loop Mode"), TTR("Ping-Pong", "Clip Loop Mode") };
	for (int mode = Animation::LOOP_NONE; mode <= Animation::LOOP_PINGPONG; mode++) {
		CheckBox *option = memnew(CheckBox(clip_mode_names[mode]));
		option->set_button_group(clip_mode_group);
		option->set_tooltip_text(TTR("Loop mode stored in the clip."));
		option->connect(SceneStringName(pressed), callable_mp(this, &AnimationWorkbenchEditorPlugin::set_loop_mode).bind(mode));
		clip_modes->add_child(option);
		clip_loop_options[mode] = option;
	}

	HFlowContainer *transport = memnew(HFlowContainer);
	controls->add_child(transport);
	HBoxContainer *playback = memnew(HBoxContainer);
	transport->add_child(playback);
	play = memnew(Button);
	play->set_name("Playback");
	play->set_toggle_mode(true);
	play->connect(SceneStringName(toggled), callable_mp(this, &AnimationWorkbenchEditorPlugin::_play_toggled));
	playback->add_child(play);
	rate = memnew(OptionButton);
	for (const String &label : { String("0.25x"), String("0.5x"), String("1x"), String("2x") }) {
		rate->add_item(label);
	}
	rate->select(2);
	rate->connect(SceneStringName(item_selected), callable_mp(this, &AnimationWorkbenchEditorPlugin::_speed_selected));
	playback->add_child(rate);
	loop = memnew(Button);
	loop->set_toggle_mode(true);
	loop->set_pressed(true);
	loop->set_tooltip_text(TTR("Loop the preview only. The clip is not changed."));
	playback->add_child(loop);
	units = memnew(OptionButton);
	units->set_name("TimelineUnits");
	units->add_item(TTR("Frames"));
	units->add_item(TTR("Seconds"));
	units->select(0);
	units->connect(SceneStringName(item_selected), callable_mp(this, &AnimationWorkbenchEditorPlugin::_units_changed));
	playback->add_child(units);
	frames_readout = memnew(Label);
	frames_readout->set_name("FrameReadout");
	frames_readout->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
	frames_readout->hide();
	playback->add_child(frames_readout);
	HBoxContainer *preview_status = memnew(HBoxContainer);
	preview_status->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	preview_status->set_alignment(BoxContainer::ALIGNMENT_END);
	preview_status->add_theme_constant_override(SNAME("separation"), 12 * EDSCALE);
	transport->add_child(preview_status);
	seconds_readout = memnew(Label);
	seconds_readout->set_name("SecondsReadout");
	seconds_readout->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
	preview_status->add_child(seconds_readout);
	transport->connect(SceneStringName(theme_changed), callable_mp(this, &AnimationWorkbenchEditorPlugin::_update_readout_widths), CONNECT_DEFERRED);
	reset = workbench_button(preview_status, TTR("Reset"), callable_mp(this, &AnimationWorkbenchEditorPlugin::_reset_preview));
	reset->set_tooltip_text(TTR("Restore the preview pose and fit the entire clip in the timeline."));
	timeline = memnew(AnimationClipTimeline);
	controls->add_child(timeline);
	timeline->connect("drag_started", callable_mp(this, &AnimationWorkbenchEditorPlugin::_pause));
	timeline->connect("value_changed", callable_mp(this, &AnimationWorkbenchEditorPlugin::_sample));
	timeline->connect("selection_changed", callable_mp(this, &AnimationWorkbenchEditorPlugin::_set_selection));
	timeline->connect("selection_cleared", callable_mp(this, &AnimationWorkbenchEditorPlugin::_update_selection_fields).bind(Vector2()));
	HFlowContainer *edit_controls = memnew(HFlowContainer);
	controls->add_child(edit_controls);
	HBoxContainer *range = memnew(HBoxContainer);
	edit_controls->add_child(range);
	range->add_child(memnew(Label(TTR("Start", "Animation Range"))));
	first = memnew(SpinBox);
	first->set_name("SelectionStart");
	first->set_custom_minimum_size(Size2(100 * EDSCALE, 0));
	first->set_accessibility_name(TTR("Selection Start"));
	first->connect("value_changed", callable_mp(this, &AnimationWorkbenchEditorPlugin::_range_changed));
	range->add_child(first);
	range->add_child(memnew(Label(TTR("End", "Animation Range"))));
	last = memnew(SpinBox);
	last->set_name("SelectionEnd");
	last->set_custom_minimum_size(Size2(100 * EDSCALE, 0));
	last->set_accessibility_name(TTR("Selection End"));
	last->connect("value_changed", callable_mp(this, &AnimationWorkbenchEditorPlugin::_range_changed));
	range->add_child(last);
	for (SpinBox *box : { first, last }) {
		box->get_line_edit()->set_placeholder(U"\u2014");
		box->set_value_hidden(true);
		box->set_fixed_decimals(true);
	}
	selection_summary = memnew(Label);
	selection_summary->set_name("SelectionSummary");
	selection_summary->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	selection_summary->set_self_modulate(Color(1, 1, 1, 0.65));
	selection_summary->hide();
	edit_controls->add_child(selection_summary);
	HBoxContainer *actions = memnew(HBoxContainer);
	actions->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	actions->set_alignment(BoxContainer::ALIGNMENT_END);
	edit_controls->add_child(actions);
	delete_button = workbench_button(actions, TTR("Delete Range"), callable_mp(this, &AnimationWorkbenchEditorPlugin::_delete_selected));
	delete_button->set_name("DeleteRange");
	delete_button->set_tooltip_text(TTR("Delete the selected range including both endpoints."));
	undo_button = workbench_button(actions, TTR("Undo"), callable_mp(this, &AnimationWorkbenchEditorPlugin::undo_edit));
	redo_button = workbench_button(actions, TTR("Redo"), callable_mp(this, &AnimationWorkbenchEditorPlugin::redo_edit));
	save_button = workbench_button(actions, TTR("Save"), callable_mp(this, &AnimationWorkbenchEditorPlugin::save_animation));
	save_button->set_tooltip_text(TTR("Save Current Clip"));
	workbench_button(actions, TTR("Save As..."), callable_mp(this, &AnimationWorkbenchEditorPlugin::_save_as_requested))->set_tooltip_text(TTR("Save Clip As..."));
	status = memnew(Label);
	status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	controls->add_child(status);
	VBoxContainer *sidebar = memnew(VBoxContainer);
	sidebar->set_custom_minimum_size(Size2(290 * EDSCALE, 0));
	split->add_child(sidebar);
	HBoxContainer *model_row = workbench_asset_card(sidebar, TTR("Preview Model"), model_settings, model_value);
	model_settings->set_name("PreviewModelSettings");
	model_value->set_name("PreviewModelName");
	model_picker = memnew(MenuButton);
	model_picker->set_name("PreviewModelPicker");
	model_picker->set_accessibility_name(TTR("Choose SKMesh..."));
	model_picker->set_tooltip_text(TTR("Select a .skmesh from the project that uses this skeleton."));
	model_picker->set_text(TTR("Modify"));
	model_picker->set_icon_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
	model_picker->get_popup()->connect("about_to_popup", callable_mp(this, &AnimationWorkbenchEditorPlugin::_refresh_model_menu));
	model_picker->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &AnimationWorkbenchEditorPlugin::_model_selected));
	model_row->add_child(model_picker);
	model_picker->add_theme_constant_override(SNAME("icon_max_width"), 8 * EDSCALE);
	model_picker->connect(SceneStringName(theme_changed), callable_mp(this, &AnimationWorkbenchEditorPlugin::_update_model_picker_icon));
	model_reveal = workbench_button(model_row, TTR("Locate"), callable_mp(this, &AnimationWorkbenchEditorPlugin::_show_preview_model));
	model_reveal->set_name("RevealPreviewModel");
	model_reveal->set_tooltip_text(TTR("Locate in FileSystem"));
	workbench_style_asset_actions(model_row);

	HBoxContainer *directory_row = workbench_asset_card(sidebar, TTR("Animation Directory"), directory_settings, directory_value);
	directory_settings->set_name("AnimationDirectorySettings");
	directory_value->set_name("AnimationDirectoryName");
	Button *directory_button = workbench_button(directory_row, TTR("Modify"), callable_mp(this, &AnimationWorkbenchEditorPlugin::_browse_animation_directory));
	directory_button->set_name("AnimationDirectoryButton");
	directory_button->set_accessibility_name(TTR("Choose the Animation Directory"));
	directory_button->set_tooltip_text(TTR("Choose the Animation Directory"));
	directory_reveal = workbench_button(directory_row, TTR("Locate"), callable_mp(this, &AnimationWorkbenchEditorPlugin::_show_animation_directory));
	directory_reveal->set_name("RevealAnimationDirectory");
	directory_reveal->set_tooltip_text(TTR("Locate in FileSystem"));
	workbench_style_asset_actions(directory_row);
	directory_picker = memnew(EditorFileDialog);
	directory_picker->set_access(EditorFileDialog::ACCESS_RESOURCES);
	directory_picker->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_DIR);
	directory_picker->set_title(TTR("Choose the Animation Directory"));
	directory_picker->connect("dir_selected", callable_mp(this, &AnimationWorkbenchEditorPlugin::_animation_directory_selected));
	add_child(directory_picker);
	model_settings->hide();
	directory_settings->hide();
	search = memnew(LineEdit);
	search->set_name("ClipSearch");
	search->set_placeholder(TTR("Search skeleton animations"));
	search->set_clear_button_enabled(true);
	search->connect(SceneStringName(text_changed), callable_mp(this, &AnimationWorkbenchEditorPlugin::_filter));
	sidebar->add_child(search);
	list = memnew(Tree);
	list->set_name("ClipList");
	// Clip and file names are data, not interface text.
	list->set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);
	list->set_hide_root(true);
	list->set_columns(2);
	list->set_column_clip_content(0, true);
	list->set_column_expand(1, false);
	list->set_column_custom_minimum_width(1, 70 * EDSCALE);
	list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	list->set_allow_rmb_select(true);
	list->connect("item_selected", callable_mp(this, &AnimationWorkbenchEditorPlugin::_list_selected));
	list->connect("item_mouse_selected", callable_mp(this, &AnimationWorkbenchEditorPlugin::_list_clicked));
	list->connect("empty_clicked", callable_mp(this, &AnimationWorkbenchEditorPlugin::_list_empty_clicked));
	// Names are edited in place and clips are dragged between libraries, like files in the FileSystem dock.
	list->connect("item_activated", callable_mp(this, &AnimationWorkbenchEditorPlugin::_list_activated));
	list->connect("item_edited", callable_mp(this, &AnimationWorkbenchEditorPlugin::_list_item_edited));
	list->connect(SceneStringName(gui_input), callable_mp(this, &AnimationWorkbenchEditorPlugin::_list_gui_input));
	list->set_drop_mode_flags(Tree::DROP_MODE_ON_ITEM);
	list->set_drag_forwarding(callable_mp(this, &AnimationWorkbenchEditorPlugin::_list_get_drag_data).bind(list), callable_mp(this, &AnimationWorkbenchEditorPlugin::_list_can_drop_data).bind(list), callable_mp(this, &AnimationWorkbenchEditorPlugin::_list_drop_data).bind(list));
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
	save_as->add_filter("*.skanim", TTR("Skeleton Animation"));
	save_as->connect("file_selected", callable_mp(this, &AnimationWorkbenchEditorPlugin::_save_as_file));
	add_child(save_as);
	rebind_dialog = memnew(ConfirmationDialog);
	rebind_dialog->set_title(TTR("Rebind Skeleton Animation"));
	rebind_dialog->set_ok_button_text(TTR("Rebind"));
	rebind_dialog->set_hide_on_ok(false);
	rebind_dialog->connect(SceneStringName(confirmed), callable_mp(this, &AnimationWorkbenchEditorPlugin::_rebind_confirmed));
	rebind_dialog->connect("canceled", callable_mp(this, &AnimationWorkbenchEditorPlugin::_rebind_cancelled));
	add_child(rebind_dialog);
	VBoxContainer *rebind_layout = memnew(VBoxContainer);
	rebind_dialog->add_child(rebind_layout);
	Label *rebind_help = memnew(Label(TTR("This .skanim is not inside a skeleton's animation directory. Choose its owning .sk to move it there.")));
	rebind_help->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	rebind_layout->add_child(rebind_help);
	rebind_clip_path = memnew(Label);
	rebind_clip_path->set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);
	rebind_clip_path->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	rebind_layout->add_child(rebind_clip_path);
	HBoxContainer *rebind_skeleton_row = memnew(HBoxContainer);
	rebind_layout->add_child(rebind_skeleton_row);
	rebind_choose_skeleton = workbench_button(rebind_skeleton_row, TTR("Choose Skeleton..."), callable_mp(this, &AnimationWorkbenchEditorPlugin::_browse_rebind_skeleton));
	rebind_skeleton_path = memnew(Label(TTR("No skeleton selected")));
	rebind_skeleton_path->set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);
	rebind_skeleton_path->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	rebind_skeleton_path->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	rebind_skeleton_row->add_child(rebind_skeleton_path);
	rebind_layout->add_child(memnew(Label(TTR("Animation Name"))));
	rebind_animation_name = memnew(LineEdit);
	rebind_animation_name->connect(SceneStringName(text_changed), callable_mp(this, &AnimationWorkbenchEditorPlugin::_rebind_name_changed));
	rebind_layout->add_child(rebind_animation_name);
	rebind_status = memnew(Label);
	rebind_status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	rebind_layout->add_child(rebind_status);
	rebind_skeleton_picker = memnew(EditorFileDialog);
	rebind_skeleton_picker->set_access(EditorFileDialog::ACCESS_RESOURCES);
	rebind_skeleton_picker->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	rebind_skeleton_picker->add_filter("*.sk", TTR("Skeleton Definition"));
	rebind_skeleton_picker->connect("file_selected", callable_mp(this, &AnimationWorkbenchEditorPlugin::_rebind_skeleton_selected));
	rebind_dialog->add_child(rebind_skeleton_picker);
	list_menu = memnew(PopupMenu);
	list_menu->connect(SceneStringName(id_pressed), callable_mp(this, &AnimationWorkbenchEditorPlugin::_list_menu_pressed));
	add_child(list_menu);
	delete_dialog = memnew(ConfirmationDialog);
	delete_dialog->set_title(TTR("Delete Animation"));
	delete_dialog->set_ok_button_text(TTR("Move to Trash"));
	delete_dialog->connect(SceneStringName(confirmed), callable_mp(this, &AnimationWorkbenchEditorPlugin::_delete_confirmed));
	add_child(delete_dialog);
	migrate_dialog = memnew(ConfirmationDialog);
	migrate_dialog->set_title(TTR("Move Clips to the Animation Directory"));
	migrate_dialog->set_ok_button_text(TTR("Move Clips"));
	migrate_dialog->connect(SceneStringName(confirmed), callable_mp(this, &AnimationWorkbenchEditorPlugin::_migrate_confirmed));
	add_child(migrate_dialog);
	_build_import_dialog();
	EditorInterface *editor = EditorInterface::get_singleton();
	editor->set_3d_asset_workspace(workspace);
	editor->connect("editor_document_restore_requested", callable_mp(this, &AnimationWorkbenchEditorPlugin::_restore_document));
	editor->connect("editor_document_selected", callable_mp(this, &AnimationWorkbenchEditorPlugin::_document_selected));
	editor->connect("scene_document_selected", callable_mp(this, &AnimationWorkbenchEditorPlugin::_leave_document));
	set_process(true);
	set_process_input(true);
	_update_camera();
}

void AnimationWorkbenchEditorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("open_player", "player"), &AnimationWorkbenchEditorPlugin::open_player);
	ClassDB::bind_method(D_METHOD("open_resource", "resource"), &AnimationWorkbenchEditorPlugin::open_resource);
	ClassDB::bind_method(D_METHOD("rebind_animation", "clip", "skeleton", "animation_name"), &AnimationWorkbenchEditorPlugin::rebind_animation);
	ClassDB::bind_method(D_METHOD("select_animation", "name"), &AnimationWorkbenchEditorPlugin::select_animation);
	ClassDB::bind_method(D_METHOD("rename_animation", "name", "new_name"), &AnimationWorkbenchEditorPlugin::rename_animation);
	ClassDB::bind_method(D_METHOD("move_animation", "name", "library"), &AnimationWorkbenchEditorPlugin::move_animation);
	ClassDB::bind_method(D_METHOD("delete_animation", "name"), &AnimationWorkbenchEditorPlugin::delete_animation);
	ClassDB::bind_method(D_METHOD("create_animation_library", "library"), &AnimationWorkbenchEditorPlugin::create_animation_library);
	ClassDB::bind_method(D_METHOD("set_animation_directory", "path"), &AnimationWorkbenchEditorPlugin::set_animation_directory);
	ClassDB::bind_method(D_METHOD("rename_animation_library", "library", "new_name"), &AnimationWorkbenchEditorPlugin::rename_animation_library);
	ClassDB::bind_method(D_METHOD("migrate_legacy_animations", "skeleton"), &AnimationWorkbenchEditorPlugin::migrate_legacy_animations);
	ClassDB::bind_method(D_METHOD("delete_range", "first", "last"), &AnimationWorkbenchEditorPlugin::delete_range);
	ClassDB::bind_static_method("AnimationWorkbenchEditorPlugin", D_METHOD("delete_clip_range", "animation", "first", "last"), &AnimationWorkbenchEditorPlugin::delete_clip_range);
	ClassDB::bind_method(D_METHOD("get_last_error"), &AnimationWorkbenchEditorPlugin::get_last_error);
	ClassDB::bind_method(D_METHOD("save_animation"), &AnimationWorkbenchEditorPlugin::save_animation);
	ClassDB::bind_method(D_METHOD("undo_edit"), &AnimationWorkbenchEditorPlugin::undo_edit);
	ClassDB::bind_method(D_METHOD("redo_edit"), &AnimationWorkbenchEditorPlugin::redo_edit);
	ClassDB::bind_method(D_METHOD("get_current_animation"), &AnimationWorkbenchEditorPlugin::get_current_animation);
	ClassDB::bind_method(D_METHOD("set_loop_mode", "loop_mode"), &AnimationWorkbenchEditorPlugin::set_loop_mode);
	ClassDB::bind_method(D_METHOD("set_preview_loop", "enabled"), &AnimationWorkbenchEditorPlugin::set_preview_loop);
	ClassDB::bind_method(D_METHOD("is_preview_loop_enabled"), &AnimationWorkbenchEditorPlugin::is_preview_loop_enabled);
	ClassDB::bind_method(D_METHOD("set_preview_speed", "speed"), &AnimationWorkbenchEditorPlugin::set_preview_speed);
	ClassDB::bind_method(D_METHOD("set_playing", "playing"), &AnimationWorkbenchEditorPlugin::set_playing);
	ClassDB::bind_method(D_METHOD("seek", "time"), &AnimationWorkbenchEditorPlugin::seek);
	ClassDB::bind_method(D_METHOD("set_selection", "first", "last"), &AnimationWorkbenchEditorPlugin::set_selection);
	ClassDB::bind_method(D_METHOD("get_state"), &AnimationWorkbenchEditorPlugin::get_workbench_state);
	ClassDB::bind_method(D_METHOD("import_files", "sources", "destination", "settings"), &AnimationWorkbenchEditorPlugin::import_files, DEFVAL(Dictionary()));
}

bool AnimationWorkbenchEditorPlugin::handles(Object *p_object) const {
	// Only skeleton assets open a clip document. Other Animation resources keep their native editors.
	const Resource *resource = Object::cast_to<Resource>(p_object);
	if (!resource || resource->is_built_in() || resource->get_path().is_empty()) {
		return false;
	}
	return Object::cast_to<SkeletonDefinition>(p_object) || (Object::cast_to<Animation>(p_object) && resource->get_path().has_extension("skanim"));
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
	Ref<Texture2D> tab_icon = EditorNode::get_singleton()->get_custom_resource_file_icon(p_path);
	if (tab_icon.is_null()) {
		tab_icon = workspace->get_theme_icon(icon, SNAME("EditorIcons"));
	}
	EditorSceneTabs::get_singleton()->open_document(p_path, label, tab_icon, callable_mp(this, &AnimationWorkbenchEditorPlugin::_activate_document).bind(p_path), callable_mp(this, &AnimationWorkbenchEditorPlugin::_save_document).bind(p_path), callable_mp(this, &AnimationWorkbenchEditorPlugin::_close_document).bind(p_path), callable_mp(this, &AnimationWorkbenchEditorPlugin::_navigate), callable_mp(this, &AnimationWorkbenchEditorPlugin::_save_as_requested), p_activate, String(), owner, ObjectDB::get_instance<Node>(p_document.player));
}

void AnimationWorkbenchEditorPlugin::open_resource(const Ref<Resource> &p_resource) {
	ERR_FAIL_COND_MSG(p_resource.is_null() || p_resource->get_path().is_empty() || p_resource->is_built_in(), TTR("Open a saved .sk or .skanim asset."));
	Ref<Animation> clip = p_resource;
	if (clip.is_valid() && p_resource->get_path().has_extension("skanim")) {
		_open_owned_clip(clip, true);
		return;
	}
	Document document;
	document.resource = p_resource;
	_register_document(p_resource->get_path(), document, true);
}

static void split_animation_name(const StringName &p_name, StringName &r_library, StringName &r_animation) {
	const String name = p_name;
	const int slash = name.find_char('/');
	r_library = slash < 0 ? StringName() : StringName(name.substr(0, slash));
	r_animation = slash < 0 ? StringName(name) : StringName(name.substr(slash + 1));
}

static StringName join_animation_name(const StringName &p_library, const StringName &p_animation) {
	return p_library == StringName() ? p_animation : StringName(String(p_library) + "/" + String(p_animation));
}

StringName AnimationWorkbenchEditorPlugin::_find_catalog_name(const Ref<SkeletonDefinition> &p_skeleton, const Ref<Animation> &p_clip) const {
	if (p_skeleton.is_null() || p_clip.is_null()) {
		return StringName();
	}
	LocalVector<StringName> libraries;
	p_skeleton->get_animation_library_list(&libraries);
	for (const StringName &library_name : libraries) {
		Ref<AnimationLibrary> library = p_skeleton->get_animation_library(library_name);
		LocalVector<StringName> names;
		library->get_animation_list(&names);
		for (const StringName &name : names) {
			if (animation_resources_match(library->get_animation(name), p_clip)) {
				return join_animation_name(library_name, name);
			}
		}
	}
	return StringName();
}

Ref<Animation> AnimationWorkbenchEditorPlugin::_catalog_clip(const StringName &p_name) const {
	if (skeleton.is_null()) {
		return Ref<Animation>();
	}
	StringName library_name;
	StringName animation_name;
	split_animation_name(p_name, library_name, animation_name);
	Ref<AnimationLibrary> library = skeleton->get_animation_library(library_name);
	return library.is_valid() && library->has_animation(animation_name) ? library->get_animation(animation_name) : Ref<Animation>();
}

static bool animation_destination_exists(const Ref<DirAccess> &p_access, const String &p_source, const String &p_target) {
	if (p_source == p_target || (p_source.nocasecmp_to(p_target) == 0 && !p_access->is_case_sensitive(p_target.get_base_dir()))) {
		return false;
	}
	return p_access->file_exists(p_target) || p_access->dir_exists(p_target);
}

Error AnimationWorkbenchEditorPlugin::_move_clip_file(const Ref<Animation> &p_clip, const String &p_path) {
	const String old_path = p_clip->get_path();
	if (old_path == p_path) {
		return OK;
	}
	Ref<DirAccess> access = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	if (animation_destination_exists(access, old_path, p_path)) {
		return ERR_ALREADY_EXISTS;
	}
	Error error = DirAccess::make_dir_recursive_absolute(p_path.get_base_dir());
	if (error != OK) {
		return error;
	}
	const ResourceUID::ID uid = ResourceLoader::get_resource_uid(old_path);
	error = access->rename(old_path, p_path);
	if (error != OK) {
		return error;
	}
	p_clip->set_path(p_path, true);
	if (uid != ResourceUID::INVALID_ID && ResourceUID::get_singleton()->has_id(uid)) {
		ResourceUID::get_singleton()->set_id(uid, p_path);
	}
	return OK;
}

bool AnimationWorkbenchEditorPlugin::_rebind_target(const Ref<Animation> &p_clip, const Ref<SkeletonDefinition> &p_skeleton, const String &p_name, String &r_target, String &r_reason) const {
	if (p_skeleton->has_baked_animation_libraries()) {
		r_reason = TTR("Open this skeleton and move its clips to the animation directory first.");
		return false;
	}
	StringName library_name;
	StringName animation_name;
	split_animation_name(p_name.strip_edges(), library_name, animation_name);
	if (!AnimationLibrary::is_valid_animation_name(animation_name) || !String(animation_name).is_valid_filename() || (library_name != StringName() && (!AnimationLibrary::is_valid_library_name(library_name) || !String(library_name).is_valid_filename()))) {
		r_reason = TTR("Enter a valid animation name without /, :, comma, or brackets.");
		return false;
	}
	if (animation_name == SNAME("RESET")) {
		r_reason = TTR("RESET is reserved for the skeleton's reset pose.");
		return false;
	}
	const String directory = p_skeleton->get_animation_directory_path();
	r_target = (library_name == StringName() ? directory : directory.path_join(library_name)).path_join(String(animation_name) + ".skanim");
	if (animation_destination_exists(DirAccess::create(DirAccess::ACCESS_RESOURCES), p_clip->get_path(), r_target)) {
		r_reason = TTR("Another clip already uses this animation name.");
		return false;
	}
	return true;
}

bool AnimationWorkbenchEditorPlugin::_open_owned_clip(const Ref<Animation> &p_clip, bool p_activate) {
	if (p_clip.is_null()) {
		return false;
	}
	Ref<SkeletonDefinition> owner = p_clip->get_skeleton();
	if (owner.is_null() || owner->get_path().is_empty() || !ResourceLoader::exists(owner->get_path())) {
		_open_rebind_dialog(p_clip);
		return false;
	}
	const StringName name = _find_catalog_name(owner, p_clip);
	if (name == StringName()) {
		_open_rebind_dialog(p_clip, owner);
		return false;
	}
	const String skeleton_path = owner->get_path();
	Document document;
	document.resource = owner;
	document.animation = name;
	documents[skeleton_path] = document;
	_register_document(skeleton_path, document, p_activate);
	EditorSceneTabs::get_singleton()->set_document_restore_path(skeleton_path, p_clip->get_path());
	return true;
}

bool AnimationWorkbenchEditorPlugin::_clip_matches_skeleton(const Ref<Animation> &p_clip, const Ref<SkeletonDefinition> &p_skeleton, String &r_reason) const {
	if (p_clip.is_null() || p_skeleton.is_null()) {
		r_reason = TTR("Choose a valid skeleton.");
		return false;
	}
	String skeleton_node = "Skeleton3D";
	const NodePath clip_skeleton_node = p_clip->get_skeleton_node_path();
	if (!clip_skeleton_node.is_empty()) {
		skeleton_node = String(clip_skeleton_node.get_concatenated_names());
	}
	PackedStringArray missing_bones;
	for (int track = 0; track < p_clip->get_track_count(); track++) {
		const Animation::TrackType type = p_clip->track_get_type(track);
		if (type != Animation::TYPE_POSITION_3D && type != Animation::TYPE_ROTATION_3D && type != Animation::TYPE_SCALE_3D) {
			continue;
		}
		const NodePath path = p_clip->track_get_path(track);
		const String target = String(path.get_concatenated_names());
		if (target != skeleton_node && !target.ends_with("/" + skeleton_node)) {
			continue;
		}
		if (path.get_subname_count() == 0) {
			continue;
		}
		const StringName bone = path.get_subname(0);
		if (p_skeleton->find_bone(bone) < 0 && !missing_bones.has(bone)) {
			missing_bones.push_back(bone);
		}
	}
	if (!missing_bones.is_empty()) {
		r_reason = vformat(TTR("The skeleton is missing bones used by this clip: %s"), String(", ").join(missing_bones.slice(0, 8)));
		return false;
	}
	r_reason.clear();
	return true;
}

bool AnimationWorkbenchEditorPlugin::_can_rebind(String &r_reason) const {
	if (rebind_clip.is_null() || rebind_skeleton.is_null()) {
		r_reason = TTR("Choose the owning .sk skeleton.");
		return false;
	}
	const String skeleton_path = rebind_skeleton->get_path();
	if (skeleton_path.is_empty() || !ResourceLoader::exists(skeleton_path)) {
		r_reason = TTR("The selected skeleton is not a saved .sk asset.");
		return false;
	}
	String target;
	if (!_rebind_target(rebind_clip, rebind_skeleton, rebind_animation_name->get_text(), target, r_reason)) {
		return false;
	}
	return _clip_matches_skeleton(rebind_clip, rebind_skeleton, r_reason);
}

void AnimationWorkbenchEditorPlugin::_open_rebind_dialog(const Ref<Animation> &p_clip, const Ref<SkeletonDefinition> &p_skeleton) {
	rebind_clip = p_clip;
	rebind_skeleton = p_skeleton;
	rebind_clip_path->set_text(p_clip.is_valid() ? p_clip->get_path() : String());
	rebind_clip_path->set_tooltip_text(rebind_clip_path->get_text());
	rebind_skeleton_path->set_text(rebind_skeleton.is_valid() ? rebind_skeleton->get_path() : TTR("No skeleton selected"));
	rebind_skeleton_path->set_tooltip_text(rebind_skeleton_path->get_text());
	StringName existing_name = _find_catalog_name(rebind_skeleton, rebind_clip);
	if (existing_name == StringName()) {
		existing_name = p_clip.is_valid() ? p_clip->get_path().get_file().get_basename() : String();
	}
	rebind_animation_name->set_text(existing_name);
	String reason;
	const bool valid = _can_rebind(reason);
	rebind_dialog->get_ok_button()->set_disabled(!valid);
	rebind_status->set_text(valid ? TTR("Ready to move this clip into the selected skeleton's animation directory.") : reason);
	rebind_dialog->popup_centered();
}

void AnimationWorkbenchEditorPlugin::_browse_rebind_skeleton() {
	rebind_skeleton_picker->set_current_path(rebind_skeleton.is_valid() ? rebind_skeleton->get_path() : rebind_clip_path->get_text().get_base_dir());
	rebind_skeleton_picker->popup_file_dialog();
}

void AnimationWorkbenchEditorPlugin::_rebind_skeleton_selected(const String &p_path) {
	Ref<SkeletonDefinition> selected = ResourceLoader::load(p_path);
	if (selected.is_null() || selected->get_path().is_empty()) {
		rebind_skeleton = Ref<SkeletonDefinition>();
		rebind_skeleton_path->set_text(TTR("No skeleton selected"));
		rebind_status->set_text(TTR("Choose a valid saved .sk skeleton."));
		rebind_dialog->get_ok_button()->set_disabled(true);
		return;
	}
	rebind_skeleton = selected;
	rebind_skeleton_path->set_text(selected->get_path());
	rebind_skeleton_path->set_tooltip_text(selected->get_path());
	_rebind_name_changed(rebind_animation_name->get_text());
}

void AnimationWorkbenchEditorPlugin::_rebind_name_changed(const String &) {
	String reason;
	const bool valid = _can_rebind(reason);
	rebind_dialog->get_ok_button()->set_disabled(!valid);
	rebind_status->set_text(valid ? TTR("Ready to move this clip into the selected skeleton's animation directory.") : reason);
}

Error AnimationWorkbenchEditorPlugin::rebind_animation(const Ref<Animation> &p_clip, const Ref<SkeletonDefinition> &p_skeleton, const StringName &p_animation_name) {
	if (p_clip.is_null() || p_clip->get_path().is_empty() || p_clip->is_built_in() || !p_clip->get_path().has_extension("skanim")) {
		last_error = TTR("Choose a saved .skanim clip to rebind.");
		return ERR_INVALID_PARAMETER;
	}
	if (p_skeleton.is_null() || p_skeleton->get_path().is_empty() || !ResourceLoader::exists(p_skeleton->get_path())) {
		last_error = TTR("Choose a saved .sk skeleton.");
		return ERR_INVALID_PARAMETER;
	}
	String target;
	String reason;
	if (!_rebind_target(p_clip, p_skeleton, p_animation_name, target, reason)) {
		last_error = reason;
		return ERR_INVALID_PARAMETER;
	}
	if (!_clip_matches_skeleton(p_clip, p_skeleton, reason)) {
		last_error = reason;
		return ERR_INVALID_DATA;
	}

	const String source = p_clip->get_path();
	const String target_dir = target.get_base_dir();
	Ref<DirAccess> access = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	Error error = access->make_dir_recursive(target_dir);
	if (error == OK && !access->dir_exists(target_dir)) {
		error = ERR_CANT_CREATE;
	}
	if (error != OK) {
		last_error = vformat(TTR("Could not move the clip into the animation directory: %s"), error_names[error]);
		return error;
	}
	const String temporary_name = ".rebind-" + uitos(OS::get_singleton()->get_ticks_usec());
	const String staged_path = target_dir.path_join(temporary_name + ".skanim");
	const String backup_path = source.get_base_dir().path_join(temporary_name + ".bak");
	if (animation_destination_exists(access, String(), staged_path) || animation_destination_exists(access, String(), backup_path)) {
		last_error = TTR("Could not prepare temporary files for rebinding the clip.");
		return ERR_ALREADY_EXISTS;
	}

	// Keep the old file untouched while preparing the replacement. Copying its header preserves the UID.
	const ResourceUID::ID uid = ResourceLoader::get_resource_uid(source);
	error = access->copy(source, staged_path);
	if (error == OK) {
		Ref<Animation> rebound = p_clip->duplicate();
		rebound->set_skeleton(p_skeleton);
		// An absolute save path keeps the editor's save callback from indexing the staged UID.
		error = ResourceSaver::save(rebound, ProjectSettings::get_singleton()->globalize_path(staged_path));
		if (error == OK) {
			Ref<Animation> saved = ResourceLoader::load(staged_path, "Animation", ResourceFormatLoader::CACHE_MODE_IGNORE);
			if (saved.is_null() || saved->get_skeleton_path() != p_skeleton->get_path() || (uid != ResourceUID::INVALID_ID && ResourceLoader::get_resource_uid(staged_path) != uid)) {
				error = ERR_FILE_CANT_WRITE;
			}
		}
	}
	if (error != OK) {
		if (access->file_exists(staged_path) && access->remove(staged_path) != OK) {
			WARN_PRINT(vformat("Could not remove temporary clip '%s'.", staged_path));
		}
		last_error = vformat(TTR("Could not save the animation clip: %s"), error_names[error]);
		return error;
	}

	// Move the original aside first. This also handles rebinding at the current path.
	error = animation_destination_exists(access, source, target) ? ERR_ALREADY_EXISTS : access->rename(source, backup_path);
	bool restored = true;
	if (error == OK) {
		error = animation_destination_exists(access, String(), target) ? ERR_ALREADY_EXISTS : access->rename(staged_path, target);
		if (error != OK) {
			restored = !animation_destination_exists(access, String(), source) && access->rename(backup_path, source) == OK;
		}
	}
	if (error != OK) {
		if (access->remove(staged_path) != OK) {
			WARN_PRINT(vformat("Could not remove temporary clip '%s'.", staged_path));
		}
		last_error = restored ? vformat(TTR("Could not move the clip into the animation directory: %s"), error_names[error]) : vformat(TTR("Could not restore the clip to its original path. The original file is at '%s'."), backup_path);
		return error;
	}
	if (access->remove(backup_path) != OK) {
		WARN_PRINT(vformat("Could not remove backup clip '%s'.", backup_path));
	}

	Ref<SkeletonDefinition> old_skeleton = ResourceCache::get_ref(p_clip->get_skeleton_path());
	p_clip->set_path(target, true);
	if (uid != ResourceUID::INVALID_ID && ResourceUID::get_singleton()->has_id(uid)) {
		ResourceUID::get_singleton()->set_id(uid, target);
	}
	p_clip->set_skeleton(p_skeleton);
	if (old_skeleton.is_valid() && old_skeleton != p_skeleton) {
		old_skeleton->rescan_animations();
	}
	p_skeleton->rescan_animations();
	last_error.clear();
	EditorFileSystem::get_singleton()->scan();
	if (rebind_dialog->is_visible() && rebind_clip == p_clip) {
		rebind_dialog->hide();
		rebind_clip = Ref<Animation>();
		rebind_skeleton = Ref<SkeletonDefinition>();
	}
	return OK;
}

void AnimationWorkbenchEditorPlugin::_rebind_confirmed() {
	const Ref<Animation> clip = rebind_clip;
	const Ref<SkeletonDefinition> owner = rebind_skeleton;
	if (clip.is_null() || owner.is_null()) {
		return;
	}
	const String skeleton_path = owner->get_path();
	const String clip_path = clip->get_path();
	const StringName animation_name = StringName(rebind_animation_name->get_text().strip_edges());
	const Error error = rebind_animation(clip, owner, animation_name);
	if (error != OK) {
		rebind_status->set_text(last_error);
		return;
	}
	rebind_dialog->hide();
	rebind_clip = Ref<Animation>();
	rebind_skeleton = Ref<SkeletonDefinition>();
	Document document;
	document.resource = owner;
	document.animation = animation_name;
	documents[skeleton_path] = document;
	_register_document(skeleton_path, document, true);
	EditorSceneTabs::get_singleton()->set_document_restore_path(skeleton_path, clip_path);
}

void AnimationWorkbenchEditorPlugin::_rebind_cancelled() {
	rebind_clip = Ref<Animation>();
	rebind_skeleton = Ref<SkeletonDefinition>();
	rebind_status->set_text(String());
}

void AnimationWorkbenchEditorPlugin::open_player(AnimationPlayer *p_player) {
	ERR_FAIL_NULL(p_player);
	for (const Variant &value : EditorInterface::get_singleton()->get_open_scene_roots()) {
		Node *root = Object::cast_to<Node>(value);
		if (root == p_player || root->is_ancestor_of(p_player)) {
			Document document;
			document.player = p_player->get_instance_id();
			document.scene = root->get_instance_id();
			const String path = EditorSceneTabs::get_singleton()->get_node_document_path(root, p_player, "AnimationPlayer");
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
	} else if (p_path.has_extension("skanim")) {
		// A skeleton tab stores its last selected clip as the document restore path.
		Ref<Animation> clip = ResourceLoader::load(p_path);
		if (clip.is_valid()) {
			_open_owned_clip(clip, false);
		}
	} else if (p_path.has_extension("sk")) {
		Document document;
		document.resource = ResourceLoader::load(p_path);
		if (document.resource.is_valid()) {
			_register_document(p_path, document, false);
		}
	}
}

bool AnimationWorkbenchEditorPlugin::_load_preview_model(const Ref<PackedScene> &p_scene, bool p_save_choice) {
	Node *instance = p_scene.is_valid() ? p_scene->instantiate() : nullptr;
	Node3D *visual = Object::cast_to<Node3D>(instance);
	AnimationPlayer *player = visual ? Object::cast_to<AnimationPlayer>(visual->get_node_or_null(NodePath("AnimationPlayer"))) : nullptr;
	if (!player || player->get_skeleton_definition() != skeleton) {
		if (p_save_choice) {
			status->set_text(TTR("The preview model must use the current skeleton."));
		}
		if (instance) {
			memdelete(instance);
		}
		return false;
	}
	if (p_save_choice) {
		const String previous = skeleton->get_preview_mesh();
		skeleton->set_preview_mesh(p_scene->get_path());
		if (ResourceSaver::save(skeleton) != OK) {
			skeleton->set_preview_mesh(previous);
			memdelete(instance);
			status->set_text(TTR("Could not save the preview model selection."));
			return false;
		}
	}
	_clear_model();
	_build_preview(visual, player);
	return true;
}

void AnimationWorkbenchEditorPlugin::_activate_document(const String &p_path) {
	callable_mp(this, &AnimationWorkbenchEditorPlugin::_inspect_document).call_deferred(p_path);
	if (current_document == p_path && model) {
		_refresh();
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
			if (copy) {
				_build_preview(copy, player);
			}
		}
		Node *root = Object::cast_to<Node>(ObjectDB::get_instance(document.scene));
		title->set_text(String(root->get_name()) + " / " + String(root->get_path_to(player)));
		model_settings->hide();
	} else {
		Ref<Animation> clip = document.resource;
		skeleton = Ref<SkeletonDefinition>(document.resource);
		if (clip.is_valid()) {
			skeleton = clip->get_skeleton();
		}
		model_settings->show();
		String preview_path = skeleton.is_valid() ? skeleton->get_preview_mesh() : String();
		if (skeleton.is_valid() && (preview_path.is_empty() || !preview_path.has_extension("skmesh") || !ResourceLoader::exists(preview_path))) {
			// Not written back: the stored default changes only through the picker.
			preview_path = _find_model(skeleton);
		}
		if (!preview_path.is_empty()) {
			Ref<PackedScene> mesh = ResourceLoader::load(preview_path);
			_load_preview_model(mesh, false);
		}
		_update_model_picker();
		AnimationPlayer *catalog_player = _get_catalog_player();
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
	if (_get_catalog_player()) {
		_refresh();
	} else {
		status->set_text(skeleton.is_valid() ? TTR("No model uses this skeleton yet. Import a model for it, or choose a preview model.") : TTR("This animation player has no 3D model to preview."));
	}
	_update_directory_row();
	if (skeleton.is_valid() && skeleton->has_baked_animation_libraries() && !skeleton->get_path().is_empty()) {
		migrate_dialog->set_text(vformat(TTR("This skeleton still lists its clips inside the .sk file.\nMove them into '%s'? File names then become the animation names, and each sub-folder becomes an animation library."), skeleton->get_animation_directory_path()));
		migrate_dialog->call_deferred("popup_centered");
	}
	view->call_deferred("grab_focus");
}

void AnimationWorkbenchEditorPlugin::_inspect_document(const String &p_path) {
	const String active_document = EditorSceneTabs::get_singleton()->get_active_document();
	if (active_document != p_path || !documents.has(p_path)) {
		return;
	}
	AnimationPlayer *player = Object::cast_to<AnimationPlayer>(ObjectDB::get_instance(documents[p_path].player));
	if (player) {
		EditorNode::get_singleton()->push_item(player, "", true);
	}
}

void AnimationWorkbenchEditorPlugin::_build_preview(Node3D *p_model, AnimationPlayer *p_catalog) {
	model = p_model;
	catalog_player_id = p_catalog->get_instance_id();
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
	p_catalog->connect("animation_list_changed", callable_mp(this, &AnimationWorkbenchEditorPlugin::_catalog_changed), CONNECT_DEFERRED);
	_frame_model();
}

static void _collect_skmesh_paths(const String &p_directory, Vector<String> &r_paths) {
	Ref<DirAccess> directory = DirAccess::open(p_directory);
	if (directory.is_null()) {
		return;
	}
	directory->list_dir_begin();
	String name = directory->get_next();
	while (!name.is_empty()) {
		if (name.begins_with(".")) {
			name = directory->get_next();
			continue;
		}
		const String path = p_directory.path_join(name);
		if (directory->current_is_dir()) {
			_collect_skmesh_paths(path, r_paths);
		} else if (name.get_extension() == "skmesh") {
			r_paths.push_back(path);
		}
		name = directory->get_next();
	}
	directory->list_dir_end();
}

Vector<String> AnimationWorkbenchEditorPlugin::_matching_models(const Ref<SkeletonDefinition> &p_skeleton) const {
	Vector<String> models;
	if (p_skeleton.is_null() || p_skeleton->get_path().is_empty()) {
		return models;
	}
	const String skeleton_path = p_skeleton->get_path();
	const String skeleton_uid = ResourceUID::get_singleton()->id_to_text(ResourceLoader::get_resource_uid(skeleton_path));
	Vector<String> candidates;
	_collect_skmesh_paths("res://", candidates);
	for (const String &path : candidates) {
		List<String> dependencies;
		ResourceLoader::get_dependencies(path, &dependencies);
		for (const String &dependency : dependencies) {
			const String id = dependency.get_slice("::", 0);
			if (id == skeleton_path || id == skeleton_uid || dependency.get_slice("::", 2) == skeleton_path) {
				models.push_back(path);
				break;
			}
		}
	}
	models.sort();
	return models;
}

void AnimationWorkbenchEditorPlugin::_refresh_model_menu() {
	PopupMenu *menu = model_picker->get_popup();
	menu->clear();
	const Vector<String> models = _matching_models(skeleton);
	if (models.is_empty()) {
		menu->add_item(TTR("No matching SKMesh in this project"));
		menu->set_item_disabled(0, true);
		return;
	}

	HashMap<String, int> name_counts;
	for (const String &path : models) {
		name_counts[path.get_file()]++;
	}
	const Ref<Texture2D> mesh_icon = workspace->get_theme_icon(SNAME("SKMesh"), SNAME("EditorIcons"));
	const String current_model = model ? model->get_scene_file_path() : String();
	for (int i = 0; i < models.size(); i++) {
		const String &path = models[i];
		const String name = path.get_file();
		const String label = name_counts[name] > 1 ? path.trim_prefix("res://") : name;
		menu->add_icon_radio_check_item(mesh_icon, label, i);
		menu->set_item_auto_translate_mode(i, Node::AUTO_TRANSLATE_MODE_DISABLED);
		menu->set_item_metadata(i, path);
		menu->set_item_tooltip(i, path);
		menu->set_item_checked(i, path == current_model);
	}
}

void AnimationWorkbenchEditorPlugin::_model_selected(int p_id) {
	PopupMenu *menu = model_picker->get_popup();
	const int index = menu->get_item_index(p_id);
	if (index < 0 || menu->is_item_disabled(index) || skeleton.is_null()) {
		return;
	}
	const String path = menu->get_item_metadata(index);
	Ref<PackedScene> packed = ResourceLoader::load(path);
	if (_load_preview_model(packed, true)) {
		_update_model_picker();
		_refresh();
	}
}

void AnimationWorkbenchEditorPlugin::_update_model_picker() {
	const String path = model ? model->get_scene_file_path() : String();
	model_value->set_text(path.is_empty() ? TTR("None") : path.get_file());
	model_value->set_tooltip_text(path.is_empty() ? TTR("Select a .skmesh from the project that uses this skeleton.") : path);
	model_reveal->set_disabled(path.is_empty() || !FileAccess::exists(path));
}

void AnimationWorkbenchEditorPlugin::_update_model_picker_icon() {
	model_picker->set_button_icon(model_picker->get_theme_icon(SNAME("GuiOptionArrow"), SNAME("EditorIcons")));
}

void AnimationWorkbenchEditorPlugin::_show_preview_model() {
	const String path = model ? model->get_scene_file_path() : String();
	if (!path.is_empty() && FileAccess::exists(path)) {
		workbench_locate_path(path);
	}
}

void AnimationWorkbenchEditorPlugin::_clear_model() {
	if (AnimationPlayer *catalog_player = _get_catalog_player()) {
		catalog_player->disconnect("animation_list_changed", callable_mp(this, &AnimationWorkbenchEditorPlugin::_catalog_changed));
	}
	catalog_player_id = ObjectID();
	current_clip = ObjectID();
	poses.clear();
	if (model) {
		memdelete(model);
	}
	model = nullptr;
	sample_player = nullptr;
	timeline->set_clip(Ref<Animation>(), 0);
	list->clear();
	clip_title->set_text(String());
	clip_title->set_tooltip_text(String());
	seconds_readout->set_text(String());
	frames_readout->set_text(String());
	_update_readout_widths();
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

void AnimationWorkbenchEditorPlugin::_prune_scene_drafts() {
	Vector<ObjectID> removed;
	for (const KeyValue<ObjectID, Draft> &entry : drafts) {
		if (entry.value.scene.is_valid() && !ObjectDB::get_instance(entry.value.scene) && entry.value.source->is_built_in()) {
			removed.push_back(entry.key);
		}
	}
	for (ObjectID id : removed) {
		drafts[id].source->disconnect_changed(callable_mp(this, &AnimationWorkbenchEditorPlugin::_source_changed).bind(id));
		drafts.erase(id);
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

void AnimationWorkbenchEditorPlugin::_draw_axis_gizmo() {
	// Shows which way the world axes point from the current camera.
	const Basis to_view = camera->get_basis().inverse();
	const Vector2 middle = axis_gizmo->get_size() / 2;
	const double radius = 30 * EDSCALE;
	const Vector3 directions[3] = { Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) };
	const Color colors[3] = { Color(0.96, 0.2, 0.32), Color(0.53, 0.84, 0.01), Color(0.16, 0.55, 0.96) };
	const String names[3] = { "X", "Y", "Z" };
	int order[3] = { 0, 1, 2 };
	Vector3 projected[3];
	for (int axis = 0; axis < 3; axis++) {
		projected[axis] = to_view.xform(directions[axis]);
	}
	// Far axes first, so the one pointing at the viewer is drawn on top.
	for (int i = 0; i < 3; i++) {
		for (int j = i + 1; j < 3; j++) {
			if (projected[order[j]].z < projected[order[i]].z) {
				SWAP(order[i], order[j]);
			}
		}
	}
	const Ref<Font> font = axis_gizmo->get_theme_default_font();
	const int font_size = axis_gizmo->get_theme_default_font_size();
	axis_gizmo->draw_circle(middle, radius + 12 * EDSCALE, Color(0, 0, 0, 0.25));
	for (int i = 0; i < 3; i++) {
		const int axis = order[i];
		const Vector2 tip = middle + Vector2(projected[axis].x, -projected[axis].y) * radius;
		axis_gizmo->draw_line(middle, tip, colors[axis], 2 * EDSCALE, true);
		axis_gizmo->draw_circle(tip, 8 * EDSCALE, colors[axis]);
		const Vector2 text_size = font->get_string_size(names[axis], HORIZONTAL_ALIGNMENT_LEFT, -1, font_size * 0.8);
		axis_gizmo->draw_string(font, tip + Vector2(-text_size.x / 2, text_size.y / 4), names[axis], HORIZONTAL_ALIGNMENT_LEFT, -1, font_size * 0.8, Color(0, 0, 0));
	}
}

void AnimationWorkbenchEditorPlugin::_update_camera() {
	if (axis_gizmo) {
		axis_gizmo->queue_redraw();
	}
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

void AnimationWorkbenchEditorPlugin::_reset_preview() {
	_pause();
	_restore_pose();
	timeline->fit();
}

void AnimationWorkbenchEditorPlugin::_restore_pose() {
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

bool AnimationWorkbenchEditorPlugin::_catalog_editable() {
	if (skeleton.is_null() || skeleton->get_animation_directory_path().is_empty()) {
		last_error = TTR("Open a saved .sk skeleton to manage its animations.");
		return false;
	}
	if (skeleton->has_baked_animation_libraries()) {
		last_error = TTR("Open this skeleton and move its clips to the animation directory first.");
		return false;
	}
	return true;
}

void AnimationWorkbenchEditorPlugin::_after_catalog_edit(const StringName &p_select) {
	last_error.clear();
	if (skeleton.is_valid()) {
		skeleton->rescan_animations();
	}
	EditorFileSystem::get_singleton()->scan();
	Document *document = documents.getptr(current_document);
	if (document && p_select != StringName()) {
		document->animation = p_select;
		Ref<Animation> clip = _catalog_clip(p_select);
		if (clip.is_valid() && Ref<SkeletonDefinition>(document->resource).is_valid()) {
			EditorSceneTabs::get_singleton()->set_document_restore_path(current_document, clip->get_path());
		}
	}
	_refresh();
	_update_directory_row();
	_update_document_status();
}

void AnimationWorkbenchEditorPlugin::_update_directory_row() {
	const String directory = skeleton.is_valid() ? skeleton->get_animation_directory_path() : String();
	directory_settings->set_visible(!directory.is_empty());
	if (directory.is_empty()) {
		return;
	}
	const String name = directory.trim_suffix("/").get_file();
	const bool exists = DirAccess::dir_exists_absolute(directory);
	directory_value->set_text((name.is_empty() ? directory : name) + (exists ? String() : " " + TTR("(missing)")));
	directory_value->set_tooltip_text(directory + "\n" + (exists ? TTR("Clip files in this folder are the skeleton's animations. Each sub-folder is an animation library.") : TTR("This folder does not exist yet. It is created when the first clip is imported or saved.")));
	directory_reveal->set_disabled(!exists);
}

void AnimationWorkbenchEditorPlugin::_browse_animation_directory() {
	if (!_catalog_editable()) {
		status->set_text(last_error);
		return;
	}
	const String directory = skeleton->get_animation_directory_path();
	directory_picker->set_current_dir(DirAccess::dir_exists_absolute(directory) ? directory : skeleton->get_path().get_base_dir());
	directory_picker->popup_file_dialog();
}

void AnimationWorkbenchEditorPlugin::_animation_directory_selected(const String &p_path) {
	if (set_animation_directory(p_path) != OK) {
		status->set_text(last_error);
	} else {
		status->set_text(TTR("Animation directory changed. Clip files were not moved."));
	}
}

void AnimationWorkbenchEditorPlugin::_show_animation_directory() {
	if (skeleton.is_valid() && DirAccess::dir_exists_absolute(skeleton->get_animation_directory_path())) {
		workbench_locate_path(skeleton->get_animation_directory_path());
	}
}

Error AnimationWorkbenchEditorPlugin::set_animation_directory(const String &p_path) {
	if (!_catalog_editable()) {
		return ERR_UNAVAILABLE;
	}
	const String path = p_path.simplify_path().trim_suffix("/");
	if (!path.begins_with("res://") || !DirAccess::dir_exists_absolute(path)) {
		last_error = TTR("Choose an existing folder inside the project.");
		return ERR_INVALID_PARAMETER;
	}
	// A folder below the skeleton is stored relative to it, so both can be moved together.
	const String base = skeleton->get_path().get_base_dir().trim_suffix("/") + "/";
	const String stored = path.begins_with(base) ? path.substr(base.length()) : path;
	const String previous = skeleton->get_animation_directory();
	if (stored == previous) {
		last_error.clear();
		return OK;
	}
	skeleton->set_animation_directory(stored);
	const Error error = ResourceSaver::save(skeleton);
	if (error != OK) {
		skeleton->set_animation_directory(previous);
		last_error = vformat(TTR("Could not save the skeleton: %s"), error_names[error]);
		return error;
	}
	const Document *document = documents.getptr(current_document);
	_after_catalog_edit(document ? document->animation : StringName());
	return OK;
}

void AnimationWorkbenchEditorPlugin::_rescan() {
	if (skeleton.is_valid()) {
		skeleton->rescan_animations();
	}
	_refresh();
}

void AnimationWorkbenchEditorPlugin::_filesystem_changed() {
	// Clips renamed, moved or added outside the workbench appear without reopening anything.
	List<Ref<Resource>> cached;
	ResourceCache::get_cached_resources(&cached);
	bool current_changed = false;
	for (const Ref<Resource> &resource : cached) {
		Ref<SkeletonDefinition> definition = resource;
		if (definition.is_valid() && definition->rescan_animations() && definition == skeleton) {
			current_changed = true;
		}
	}
	if (!_get_catalog_player() || !visible) {
		return;
	}
	if (current_changed) {
		_refresh();
	} else {
		_filter(search->get_text());
	}
}

Error AnimationWorkbenchEditorPlugin::rename_animation(const StringName &p_name, const String &p_new_name) {
	if (!_catalog_editable()) {
		return ERR_UNAVAILABLE;
	}
	Ref<Animation> clip = _catalog_clip(p_name);
	if (clip.is_null()) {
		last_error = vformat(TTR("Animation not found: %s"), p_name);
		return ERR_DOES_NOT_EXIST;
	}
	const String new_name = p_new_name.strip_edges();
	if (!AnimationLibrary::is_valid_animation_name(new_name) || !new_name.is_valid_filename()) {
		last_error = TTR("Enter a valid animation name without /, :, comma, or brackets.");
		return ERR_INVALID_PARAMETER;
	}
	StringName library_name;
	StringName animation_name;
	split_animation_name(p_name, library_name, animation_name);
	const Error error = _move_clip_file(clip, clip->get_path().get_base_dir().path_join(new_name + ".skanim"));
	if (error != OK) {
		last_error = error == ERR_ALREADY_EXISTS ? TTR("Another clip already uses this animation name.") : vformat(TTR("Could not rename the clip file: %s"), error_names[error]);
		return error;
	}
	_after_catalog_edit(join_animation_name(library_name, new_name));
	return OK;
}

Error AnimationWorkbenchEditorPlugin::move_animation(const StringName &p_name, const StringName &p_library) {
	if (!_catalog_editable()) {
		return ERR_UNAVAILABLE;
	}
	Ref<Animation> clip = _catalog_clip(p_name);
	if (clip.is_null()) {
		last_error = vformat(TTR("Animation not found: %s"), p_name);
		return ERR_DOES_NOT_EXIST;
	}
	const String library = String(p_library).strip_edges();
	if (!library.is_empty() && (!AnimationLibrary::is_valid_library_name(library) || !library.is_valid_filename())) {
		last_error = TTR("Enter a valid library name without /, :, comma, or brackets.");
		return ERR_INVALID_PARAMETER;
	}
	StringName library_name;
	StringName animation_name;
	split_animation_name(p_name, library_name, animation_name);
	const String directory = skeleton->get_animation_directory_path();
	const Error error = _move_clip_file(clip, (library.is_empty() ? directory : directory.path_join(library)).path_join(String(animation_name) + ".skanim"));
	if (error != OK) {
		last_error = error == ERR_ALREADY_EXISTS ? TTR("The target library already has an animation with this name.") : vformat(TTR("Could not move the clip file: %s"), error_names[error]);
		return error;
	}
	_after_catalog_edit(join_animation_name(library, animation_name));
	return OK;
}

Error AnimationWorkbenchEditorPlugin::delete_animation(const StringName &p_name) {
	if (!_catalog_editable()) {
		return ERR_UNAVAILABLE;
	}
	Ref<Animation> clip = _catalog_clip(p_name);
	if (clip.is_null()) {
		last_error = vformat(TTR("Animation not found: %s"), p_name);
		return ERR_DOES_NOT_EXIST;
	}
	const Error error = OS::get_singleton()->move_to_trash(ProjectSettings::get_singleton()->globalize_path(clip->get_path()));
	if (error != OK) {
		last_error = vformat(TTR("Could not move the clip file to the trash: %s"), error_names[error]);
		return error;
	}
	const ObjectID clip_id = clip->get_instance_id();
	drafts.erase(clip_id);
	if (current_clip == clip_id) {
		current_clip = ObjectID();
		timeline->set_clip(Ref<Animation>(), 0);
		clip_title->set_text(String());
		clip_title->set_tooltip_text(String());
	}
	// A new file with the same name must not resolve to the deleted clip.
	clip->set_path(String());
	_after_catalog_edit(StringName());
	_update_controls();
	return OK;
}

Error AnimationWorkbenchEditorPlugin::create_animation_library(const String &p_library) {
	if (!_catalog_editable()) {
		return ERR_UNAVAILABLE;
	}
	const String library = p_library.strip_edges();
	if (!AnimationLibrary::is_valid_library_name(library) || !library.is_valid_filename()) {
		last_error = TTR("Enter a valid library name without /, :, comma, or brackets.");
		return ERR_INVALID_PARAMETER;
	}
	const String path = skeleton->get_animation_directory_path().path_join(library);
	if (DirAccess::dir_exists_absolute(path)) {
		last_error = TTR("A library with this name already exists.");
		return ERR_ALREADY_EXISTS;
	}
	const Error error = DirAccess::make_dir_recursive_absolute(path);
	if (error != OK) {
		last_error = vformat(TTR("Could not create the library folder: %s"), error_names[error]);
		return error;
	}
	_after_catalog_edit(StringName());
	return OK;
}

Error AnimationWorkbenchEditorPlugin::rename_animation_library(const StringName &p_library, const String &p_new_name) {
	if (!_catalog_editable()) {
		return ERR_UNAVAILABLE;
	}
	Ref<AnimationLibrary> library = skeleton->get_animation_library(p_library);
	if (p_library == StringName() || library.is_null()) {
		last_error = TTR("Choose a library folder. The default library is the animation directory itself.");
		return ERR_DOES_NOT_EXIST;
	}
	const String new_name = p_new_name.strip_edges();
	if (!AnimationLibrary::is_valid_library_name(new_name) || !new_name.is_valid_filename()) {
		last_error = TTR("Enter a valid library name without /, :, comma, or brackets.");
		return ERR_INVALID_PARAMETER;
	}
	const String directory = skeleton->get_animation_directory_path();
	const String old_path = directory.path_join(p_library);
	const String new_path = directory.path_join(new_name);
	if (old_path == new_path) {
		last_error.clear();
		return OK;
	}
	Ref<DirAccess> access = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	if (animation_destination_exists(access, old_path, new_path)) {
		last_error = TTR("A library with this name already exists.");
		return ERR_ALREADY_EXISTS;
	}
	LocalVector<StringName> names;
	library->get_animation_list(&names);
	Vector<Ref<Animation>> clips;
	Vector<ResourceUID::ID> uids;
	for (const StringName &name : names) {
		Ref<Animation> clip = library->get_animation(name);
		clips.push_back(clip);
		uids.push_back(ResourceLoader::get_resource_uid(clip->get_path()));
	}
	const Error error = access->rename(old_path, new_path);
	if (error != OK) {
		last_error = vformat(TTR("Could not rename the library folder: %s"), error_names[error]);
		return error;
	}
	for (int i = 0; i < clips.size(); i++) {
		const String path = new_path.path_join(clips[i]->get_path().get_file());
		clips.write[i]->set_path(path, true);
		if (uids[i] != ResourceUID::INVALID_ID && ResourceUID::get_singleton()->has_id(uids[i])) {
			ResourceUID::get_singleton()->set_id(uids[i], path);
		}
	}
	StringName selected;
	const Document *document = documents.getptr(current_document);
	if (document) {
		StringName library_name;
		StringName animation_name;
		split_animation_name(document->animation, library_name, animation_name);
		if (library_name == p_library) {
			selected = join_animation_name(new_name, animation_name);
		}
	}
	_after_catalog_edit(selected);
	return OK;
}

Error AnimationWorkbenchEditorPlugin::migrate_skeleton_animations(const Ref<SkeletonDefinition> &p_skeleton, String &r_error) {
	if (p_skeleton.is_null() || p_skeleton->get_animation_directory_path().is_empty()) {
		r_error = TTR("Choose a saved .sk skeleton.");
		return ERR_INVALID_PARAMETER;
	}
	if (!p_skeleton->has_baked_animation_libraries()) {
		return OK;
	}
	const String directory = p_skeleton->get_animation_directory_path();
	const Dictionary baked = p_skeleton->get_baked_animation_libraries().duplicate();
	Ref<DirAccess> access = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	Vector<Ref<Animation>> clips;
	Vector<String> sources;
	Vector<String> targets;
	HashSet<String> used;
	for (const KeyValue<Variant, Variant> &entry : baked) {
		const String library_name = StringName(entry.key);
		Ref<AnimationLibrary> library = entry.value;
		if (!library_name.is_empty() && (!AnimationLibrary::is_valid_library_name(library_name) || !library_name.is_valid_filename())) {
			r_error = vformat(TTR("The library name '%s' cannot be used as a folder name."), library_name);
			return ERR_INVALID_DATA;
		}
		LocalVector<StringName> names;
		library->get_animation_list(&names);
		for (const StringName &name : names) {
			Ref<Animation> clip = library->get_animation(name);
			if (clip.is_null() || clip->is_built_in() || !clip->get_path().has_extension("skanim")) {
				r_error = vformat(TTR("Animation '%s' is not a standalone .skanim file."), name);
				return ERR_INVALID_DATA;
			}
			if (clip->get_skeleton_path() != p_skeleton->get_path()) {
				// Clips of another skeleton were never part of this catalog.
				continue;
			}
			if (!String(name).is_valid_filename()) {
				r_error = vformat(TTR("The animation name '%s' cannot be used as a file name."), name);
				return ERR_INVALID_DATA;
			}
			const String target = (library_name.is_empty() ? directory : directory.path_join(library_name)).path_join(String(name) + ".skanim");
			if (used.has(target.to_lower()) || animation_destination_exists(access, clip->get_path(), target)) {
				r_error = vformat(TTR("The file '%s' already exists."), target);
				return ERR_ALREADY_EXISTS;
			}
			used.insert(target.to_lower());
			clips.push_back(clip);
			sources.push_back(clip->get_path());
			targets.push_back(target);
		}
	}
	int moved = 0;
	auto restore_clips = [&]() {
		while (moved > 0) {
			moved--;
			if (_move_clip_file(clips[moved], sources[moved]) != OK) {
				r_error += "\n" + vformat(TTR("Could not restore the clip to its original path. The original file is at '%s'."), clips[moved]->get_path());
			}
		}
	};
	for (; moved < clips.size(); moved++) {
		const Error error = _move_clip_file(clips[moved], targets[moved]);
		if (error != OK) {
			r_error = vformat(TTR("Could not move the clip files: %s"), error_names[error]);
			restore_clips();
			return error;
		}
	}
	p_skeleton->set_baked_animation_libraries(Dictionary());
	const Error error = ResourceSaver::save(p_skeleton);
	if (error != OK) {
		r_error = vformat(TTR("Could not save the skeleton: %s"), error_names[error]);
		restore_clips();
		p_skeleton->set_baked_animation_libraries(baked);
		return error;
	}
	p_skeleton->rescan_animations();
	return OK;
}

Error AnimationWorkbenchEditorPlugin::migrate_legacy_animations(const Ref<SkeletonDefinition> &p_skeleton) {
	String reason;
	const Error error = migrate_skeleton_animations(p_skeleton, reason);
	if (error != OK) {
		last_error = reason;
		return error;
	}
	last_error.clear();
	EditorFileSystem::get_singleton()->scan();
	return OK;
}

void AnimationWorkbenchEditorPlugin::_migrate_confirmed() {
	if (skeleton.is_null()) {
		return;
	}
	if (migrate_legacy_animations(skeleton) != OK) {
		status->set_text(last_error);
		return;
	}
	const Document *document = documents.getptr(current_document);
	_after_catalog_edit(document ? document->animation : StringName());
	status->set_text(TTR("Clips moved. File names are now the animation names."));
}

void AnimationWorkbenchEditorPlugin::_list_clicked(const Vector2 &p_position, MouseButton p_button) {
	if (p_button != MouseButton::RIGHT) {
		return;
	}
	list_target = String();
	TreeItem *item = list->get_selected();
	const Variant item_metadata = item ? item->get_metadata(0) : Variant();
	if (item_metadata.get_type() == Variant::STRING_NAME) {
		list_target = "animation:" + String(item_metadata);
	} else if (item_metadata.get_type() == Variant::STRING) {
		list_target = item_metadata;
	}
	_popup_list_menu(p_position);
}

void AnimationWorkbenchEditorPlugin::_list_empty_clicked(const Vector2 &p_position, MouseButton p_button) {
	if (p_button == MouseButton::RIGHT) {
		list_target = String();
		_popup_list_menu(p_position);
	}
}

void AnimationWorkbenchEditorPlugin::_popup_list_menu(const Vector2 &p_position) {
	if (!_catalog_editable()) {
		status->set_text(last_error);
		return;
	}
	list_menu->clear();
	menu_libraries.clear();
	if (list_target.begins_with("animation:")) {
		StringName library_name;
		StringName animation_name;
		split_animation_name(list_target.substr(10), library_name, animation_name);
		list_menu->add_item(TTR("Rename"), LIST_RENAME, Key::F2);
		LocalVector<StringName> libraries;
		skeleton->get_animation_library_list(&libraries);
		for (const StringName &library : libraries) {
			if (library != library_name) {
				list_menu->add_item(vformat(TTR("Move to %s"), library == StringName() ? TTR("Skeleton Animations") : String(library)), LIST_MOVE_BASE + menu_libraries.size());
				menu_libraries.push_back(library);
			}
		}
		list_menu->add_item(TTR("Show in FileSystem"), LIST_SHOW);
		list_menu->add_item(TTR("Delete"), LIST_DELETE, Key::KEY_DELETE);
		list_menu->add_separator();
	} else if (list_target.begins_with("library:")) {
		if (list_target != "library:") {
			list_menu->add_item(TTR("Rename"), LIST_RENAME, Key::F2);
		}
		list_menu->add_item(TTR("Show in FileSystem"), LIST_SHOW);
		list_menu->add_separator();
	} else if (list_target.begins_with("issue:")) {
		list_menu->add_item(TTR("Rebind to This Skeleton..."), LIST_REBIND);
		list_menu->add_item(TTR("Show in FileSystem"), LIST_SHOW);
		list_menu->add_separator();
	}
	list_menu->add_item(TTR("New Library"), LIST_NEW_LIBRARY);
	list_menu->set_position(list->get_screen_position() + p_position);
	list_menu->reset_size();
	list_menu->popup();
}

void AnimationWorkbenchEditorPlugin::_list_menu_pressed(int p_id) {
	const String target = list_target.substr(list_target.find_char(':') + 1);
	if (p_id >= LIST_MOVE_BASE) {
		const int index = p_id - LIST_MOVE_BASE;
		if (index < menu_libraries.size() && move_animation(target, menu_libraries[index]) != OK) {
			status->set_text(last_error);
		}
		return;
	}
	switch (p_id) {
		case LIST_RENAME: {
			_begin_inline_rename();
		} break;
		case LIST_NEW_LIBRARY: {
			_new_library_inline();
		} break;
		case LIST_DELETE: {
			_request_delete(target);
		} break;
		case LIST_SHOW: {
			String path = target;
			if (list_target.begins_with("animation:")) {
				Ref<Animation> clip = _catalog_clip(target);
				path = clip.is_valid() ? clip->get_path() : String();
			} else if (list_target.begins_with("library:")) {
				path = target.is_empty() ? skeleton->get_animation_directory_path() : skeleton->get_animation_directory_path().path_join(target);
			}
			if (!path.is_empty()) {
				EditorInterface::get_singleton()->select_file(path);
			}
		} break;
		case LIST_REBIND: {
			Ref<Animation> clip = ResourceLoader::load(target, "Animation");
			if (clip.is_valid()) {
				_open_rebind_dialog(clip, skeleton);
			}
		} break;
		default:
			break;
	}
}

void AnimationWorkbenchEditorPlugin::_request_delete(const StringName &p_name) {
	Ref<Animation> clip = _catalog_clip(p_name);
	if (clip.is_null() || !_catalog_editable()) {
		return;
	}
	list_target = "animation:" + String(p_name);
	delete_dialog->set_text(vformat(TTR("Move this clip file to the system trash?\n%s"), clip->get_path()));
	delete_dialog->popup_centered();
}

void AnimationWorkbenchEditorPlugin::_list_activated() {
	_begin_inline_rename();
}

void AnimationWorkbenchEditorPlugin::_list_gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> key = p_event;
	if (key.is_null() || !key->is_pressed() || key->is_echo()) {
		return;
	}
	TreeItem *item = list->get_selected();
	if (!item) {
		return;
	}
	if (key->get_keycode() == Key::F2) {
		_begin_inline_rename();
		list->accept_event();
	} else if (key->get_keycode() == Key::KEY_DELETE && item->get_metadata(0).get_type() == Variant::STRING_NAME) {
		_request_delete(item->get_metadata(0));
		list->accept_event();
	}
}

void AnimationWorkbenchEditorPlugin::_begin_inline_rename() {
	TreeItem *item = list->get_selected();
	if (!item) {
		return;
	}
	const Variant item_metadata = item->get_metadata(0);
	String text;
	if (item_metadata.get_type() == Variant::STRING_NAME) {
		StringName library_name;
		StringName animation_name;
		split_animation_name(item_metadata, library_name, animation_name);
		rename_target = "animation:" + String(item_metadata);
		text = animation_name;
	} else if (item_metadata.get_type() == Variant::STRING && String(item_metadata).begins_with("library:") && String(item_metadata) != "library:") {
		rename_target = item_metadata;
		text = String(item_metadata).substr(8);
	} else {
		return;
	}
	if (!_catalog_editable()) {
		status->set_text(last_error);
		return;
	}
	// The row shows decorations such as the unsaved marker. Only the name itself is edited.
	item->set_text(0, text);
	item->set_editable(0, true);
	list->edit_selected(true);
}

void AnimationWorkbenchEditorPlugin::_list_item_edited() {
	TreeItem *item = list->get_edited();
	if (!item || rename_target.is_empty()) {
		return;
	}
	item->set_editable(0, false);
	// The list is rebuilt by a rename, which must not happen while the tree reports the edit.
	callable_mp(this, &AnimationWorkbenchEditorPlugin::_apply_inline_rename).call_deferred(rename_target, item->get_text(0));
	rename_target = String();
}

void AnimationWorkbenchEditorPlugin::_apply_inline_rename(const String &p_target, const String &p_text) {
	const String target = p_target.substr(p_target.find_char(':') + 1);
	const String text = p_text.strip_edges();
	Error error = OK;
	bool renamed = false;
	if (p_target.begins_with("animation:")) {
		StringName library_name;
		StringName animation_name;
		split_animation_name(target, library_name, animation_name);
		if (text != String(animation_name)) {
			error = rename_animation(target, text);
			renamed = error == OK;
		}
	} else if (text != target) {
		error = rename_animation_library(target, text);
		renamed = error == OK;
	}
	if (error != OK) {
		status->set_text(last_error);
	} else if (renamed) {
		status->set_text(TTR("Renamed. Scripts and animation graphs that use the old name need updating."));
	}
	if (!renamed) {
		_filter(search->get_text());
	}
}

void AnimationWorkbenchEditorPlugin::_new_library_inline() {
	if (!_catalog_editable()) {
		status->set_text(last_error);
		return;
	}
	// The folder exists right away under a free name, then its row opens for renaming.
	const String directory = skeleton->get_animation_directory_path();
	String library = "NewLibrary";
	for (int i = 2; DirAccess::dir_exists_absolute(directory.path_join(library)); i++) {
		library = "NewLibrary" + itos(i);
	}
	if (create_animation_library(library) != OK) {
		status->set_text(last_error);
		return;
	}
	TreeItem *root = list->get_root();
	for (TreeItem *item = root ? root->get_first_child() : nullptr; item; item = item->get_next()) {
		if (item->get_metadata(0) == Variant("library:" + library)) {
			item->select(0);
			list->scroll_to_item(item);
			_begin_inline_rename();
			break;
		}
	}
}

bool AnimationWorkbenchEditorPlugin::_drop_library(const Point2 &p_point, const Variant &p_data, StringName &r_library) const {
	if (p_data.get_type() != Variant::DICTIONARY || skeleton.is_null() || skeleton->has_baked_animation_libraries()) {
		return false;
	}
	const Dictionary drag_data = p_data;
	if (String(drag_data.get("type", String())) != "skeleton_animation") {
		return false;
	}
	TreeItem *item = list->get_item_at_position(p_point);
	if (!item) {
		return false;
	}
	StringName source_library;
	StringName animation_name;
	split_animation_name(drag_data["animation"], source_library, animation_name);
	const Variant item_metadata = item->get_metadata(0);
	if (item_metadata.get_type() == Variant::STRING_NAME) {
		// Dropping on a clip targets the library that clip is in.
		StringName target_animation;
		split_animation_name(item_metadata, r_library, target_animation);
	} else if (item_metadata.get_type() == Variant::STRING && String(item_metadata).begins_with("library:")) {
		r_library = String(item_metadata).substr(8);
	} else {
		return false;
	}
	return r_library != source_library;
}

Variant AnimationWorkbenchEditorPlugin::_list_get_drag_data(const Point2 &p_point, Control *p_from) {
	TreeItem *item = list->get_item_at_position(p_point);
	if (!item || item->get_metadata(0).get_type() != Variant::STRING_NAME || skeleton.is_null() || skeleton->has_baked_animation_libraries()) {
		return Variant();
	}
	Dictionary drag_data;
	drag_data["type"] = "skeleton_animation";
	drag_data["animation"] = item->get_metadata(0);
	Label *preview = memnew(Label(String(item->get_metadata(0))));
	preview->set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);
	list->set_drag_preview(preview);
	return drag_data;
}

bool AnimationWorkbenchEditorPlugin::_list_can_drop_data(const Point2 &p_point, const Variant &p_data, Control *p_from) const {
	StringName library;
	return _drop_library(p_point, p_data, library);
}

void AnimationWorkbenchEditorPlugin::_list_drop_data(const Point2 &p_point, const Variant &p_data, Control *p_from) {
	StringName library;
	if (!_drop_library(p_point, p_data, library)) {
		return;
	}
	const Dictionary drag_data = p_data;
	if (move_animation(drag_data["animation"], library) != OK) {
		status->set_text(last_error);
	} else {
		status->set_text(TTR("Moved. Scripts and animation graphs that use the old name need updating."));
	}
}

void AnimationWorkbenchEditorPlugin::_delete_confirmed() {
	if (delete_animation(list_target.substr(list_target.find_char(':') + 1)) != OK) {
		status->set_text(last_error);
	}
}

void AnimationWorkbenchEditorPlugin::_filter(const String &p_query) {
	const String query = p_query.strip_edges().to_lower();
	updating = true;
	list->clear();
	TreeItem *root = list->create_item();
	if (AnimationPlayer *catalog_player = _get_catalog_player()) {
		LocalVector<StringName> libraries;
		catalog_player->get_animation_library_list(&libraries);
		for (const StringName &library_name : libraries) {
			Ref<AnimationLibrary> library = catalog_player->get_animation_library(library_name);
			const bool from_skeleton = skeleton.is_valid() && skeleton->get_animation_library(library_name) == library;
			LocalVector<StringName> names;
			library->get_animation_list(&names);
			TreeItem *group = list->create_item(root);
			if (library_name == StringName()) {
				group->set_text(0, from_skeleton ? TTR("Skeleton Animations") : TTR("Default Library"));
			} else {
				group->set_text(0, library_name);
			}
			group->set_icon(0, list->get_theme_icon(from_skeleton ? SNAME("SKSkeleton") : SNAME("AnimationPlayer"), EditorStringName(EditorIcons)));
			group->set_tooltip_text(0, from_skeleton ? TTR("Provided by the skeleton") : TTR("Configured on this AnimationPlayer"));
			// Library rows carry a plain string, clip rows a StringName.
			group->set_metadata(0, "library:" + String(library_name));
			group->set_selectable(1, false);
			int count = 0;
			for (const StringName &name : names) {
				const StringName full_name = library_name == StringName() ? name : StringName(String(library_name) + "/" + String(name));
				Ref<Animation> clip = library->get_animation(name);
				const String searchable = String(full_name) + "\n" + clip->get_path();
				if (!query.is_empty() && !searchable.to_lower().contains(query)) {
					continue;
				}
				const bool dirty = drafts.has(clip->get_instance_id()) && drafts[clip->get_instance_id()].dirty();
				TreeItem *item = list->create_item(group);
				item->set_text(0, (dirty ? "* " : "") + String(name));
				item->set_text(1, workbench_seconds(clip->get_length()) + " s");
				item->set_metadata(0, full_name);
				item->set_tooltip_text(0, String(full_name) + "\n" + clip->get_path() + (dirty ? "\n" + TTR("Unsaved changes") : String()));
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
	if (skeleton.is_valid() && query.is_empty()) {
		const TypedArray<Dictionary> issues = skeleton->get_animation_scan_issues();
		if (!issues.is_empty()) {
			const Color warning = list->get_theme_color(SNAME("warning_color"), EditorStringName(Editor));
			TreeItem *group = list->create_item(root);
			group->set_text(0, TTR("Needs Attention"));
			group->set_text(1, itos(issues.size()));
			group->set_custom_color(0, warning);
			group->set_selectable(0, false);
			group->set_selectable(1, false);
			for (const Dictionary issue : issues) {
				const String path = issue["path"];
				TreeItem *item = list->create_item(group);
				item->set_text(0, path.get_file());
				item->set_custom_color(0, warning);
				item->set_metadata(0, "issue:" + path);
				item->set_selectable(1, false);
				item->set_tooltip_text(0, path + "\n" + String(issue["message"]));
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
		callable_mp(this, &AnimationWorkbenchEditorPlugin::_select).call_deferred(name, playing);
	}
}

void AnimationWorkbenchEditorPlugin::select_animation(const StringName &p_name) {
	_select(p_name, playing);
}

void AnimationWorkbenchEditorPlugin::_select(const StringName &p_name, bool p_play) {
	AnimationPlayer *catalog_player = _get_catalog_player();
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
	StringName library_name;
	StringName animation_name;
	split_animation_name(p_name, library_name, animation_name);
	drafts[current_clip].library = catalog_player->get_animation_library(library_name);
	drafts[current_clip].scene = documents[current_document].scene;
	documents[current_document].animation = p_name;
	_render_draft();
	if (p_play) {
		_play_toggled(true);
	}
}

void AnimationWorkbenchEditorPlugin::_refresh() {
	AnimationPlayer *catalog_player = _get_catalog_player();
	if (!catalog_player) {
		return;
	}
	LocalVector<StringName> names;
	catalog_player->get_animation_list(&names);
	if (names.is_empty()) {
		if (current_clip.is_valid()) {
			_restore_pose();
			current_clip = ObjectID();
			timeline->set_clip(Ref<Animation>(), 0);
			clip_title->set_text(String());
			clip_title->set_tooltip_text(String());
			seconds_readout->set_text(String());
			frames_readout->set_text(String());
			_update_readout_widths();
			_update_controls();
			_update_document_status();
		}
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
	_select(selected, playing);
}

Ref<Animation> AnimationWorkbenchEditorPlugin::get_current_animation() const {
	return drafts.has(current_clip) ? drafts[current_clip].current() : Ref<Animation>();
}

static bool workbench_pose_track(Animation::TrackType p_type) {
	return p_type == Animation::TYPE_POSITION_3D || p_type == Animation::TYPE_ROTATION_3D || p_type == Animation::TYPE_SCALE_3D || p_type == Animation::TYPE_BLEND_SHAPE;
}

void AnimationWorkbenchEditorPlugin::_render_draft() {
	_restore_pose();
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
	const bool show_frames = prefer_frames && fps > 0;
	units->set_item_disabled(0, fps <= 0);
	units->select(show_frames ? 0 : 1);
	timeline->set_clip(clip, fps);
	timeline->set_frames(show_frames);
	_update_readout_widths();
	_update_readout_emphasis();
	_update_selection_fields(Vector2());
	_sample(0);
	const String clip_name = documents[current_document].animation;
	clip_title->set_text((draft.dirty() ? "* " : "") + clip_name);
	clip_title->set_tooltip_text(clip_name);
	_update_controls();
	_update_document_status();
	_filter(search->get_text());
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
	const Ref<Animation> clip = drafts[current_clip].current();
	const double fps = timeline->get_sample_rate();
	seconds_readout->set_text(workbench_seconds(time) + " / " + workbench_seconds(clip->get_length()) + " s");
	if (fps > 0) {
		const int64_t last_frame = timeline->get_last_frame();
		frames_readout->set_text(itos(MIN(int64_t(Math::round(time * fps)), last_frame)) + " / " + itos(last_frame + 1) + " " + TTR("Frames"));
	} else {
		frames_readout->set_text(String());
	}
}

static void workbench_reserve_readout_width(Label *p_label, String p_example) {
	const Ref<Font> font = p_label->get_theme_font(SNAME("font"));
	const int font_size = p_label->get_theme_font_size(SNAME("font_size"));
	char32_t widest_digit = U'0';
	real_t widest = 0;
	for (char32_t digit = U'0'; digit <= U'9'; digit++) {
		const real_t width = font->get_char_size(digit, font_size).x;
		if (width > widest) {
			widest = width;
			widest_digit = digit;
		}
	}
	for (int i = 0; i < p_example.length(); i++) {
		const char32_t character = p_example[i];
		if (character >= U'0' && character <= U'9') {
			p_example[i] = widest_digit;
		}
	}
	const real_t text_width = font->get_string_size(p_example, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x;
	const real_t style_width = p_label->get_theme_stylebox(CoreStringName(normal))->get_minimum_size().x;
	p_label->set_custom_minimum_size(Size2(Math::ceil(text_width + style_width), 0));
}

void AnimationWorkbenchEditorPlugin::_update_readout_widths() {
	if (!workspace) {
		return;
	}
	const Ref<Animation> clip = get_current_animation();
	const bool has_frames = clip.is_valid() && timeline->get_sample_rate() > 0;
	frames_readout->set_visible(has_frames);
	if (has_frames) {
		const String frame_capacity = itos(MAX(int64_t(999), timeline->get_last_frame() + 1));
		workbench_reserve_readout_width(frames_readout, frame_capacity + " / " + frame_capacity + " " + TTR("Frames"));
	}
	if (clip.is_valid()) {
		const String total_seconds = workbench_seconds(clip->get_length());
		workbench_reserve_readout_width(seconds_readout, total_seconds + " / " + total_seconds + " s");
	} else {
		seconds_readout->set_custom_minimum_size(Size2());
	}
}

void AnimationWorkbenchEditorPlugin::_update_readout_emphasis() {
	const Color primary(1, 1, 1);
	const Color secondary(1, 1, 1, 0.65);
	const bool frame_mode = timeline->is_frame_mode();
	seconds_readout->set_self_modulate(frame_mode ? secondary : primary);
	frames_readout->set_self_modulate(frame_mode ? primary : secondary);
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
	Vector2 selected = timeline->get_selection();
	if (p_first > p_last) {
		SWAP(selected.x, selected.y);
	}
	_update_selection_fields(selected);
}

void AnimationWorkbenchEditorPlugin::_update_selection_fields(const Vector2 &p_selection) {
	Ref<Animation> clip = get_current_animation();
	const bool frames = timeline->is_frame_mode();
	const double fps = timeline->get_sample_rate();
	updating = true;
	for (SpinBox *box : { first, last }) {
		box->set_step(frames ? 1 : WORKBENCH_SECONDS_STEP);
		box->set_max(frames ? double(timeline->get_last_frame()) : (clip.is_valid() ? Math::ceil(clip->get_length()) : 0));
		box->set_suffix(frames ? TTR("Frames") : TTR("Seconds"));
	}
	first->set_value(frames ? Math::round(p_selection.x * fps) : double(p_selection.x));
	last->set_value(frames ? Math::round(p_selection.y * fps) : double(p_selection.y));
	updating = false;
	_update_controls();
}

void AnimationWorkbenchEditorPlugin::_range_changed(double p_value) {
	if (updating) {
		return;
	}
	const double factor = timeline->is_frame_mode() ? timeline->get_sample_rate() : 1;
	_set_selection(timeline->snap_selection(first->get_value() / factor), timeline->snap_selection(last->get_value() / factor));
}

void AnimationWorkbenchEditorPlugin::_units_changed(int p_index) {
	prefer_frames = p_index == 0;
	timeline->set_frames(prefer_frames);
	_update_readout_emphasis();
	_update_selection_fields(timeline->get_selection());
}

void AnimationWorkbenchEditorPlugin::_update_controls() {
	const Draft *draft = drafts.getptr(current_clip);
	play->set_disabled(!draft);
	for (int mode = Animation::LOOP_NONE; mode <= Animation::LOOP_PINGPONG; mode++) {
		clip_loop_options[mode]->set_disabled(!draft);
		clip_loop_options[mode]->set_pressed_no_signal(draft && int(draft->current()->get_loop_mode()) == mode);
	}
	const bool has_selection = draft && timeline->has_selection();
	const bool show_endpoints = draft && timeline->has_selection_endpoints();
	first->set_value_hidden(!show_endpoints);
	last->set_value_hidden(!show_endpoints);
	delete_button->set_disabled(!has_selection);
	selection_summary->set_visible(has_selection);
	if (has_selection) {
		const double duration = timeline->get_cut_end() - timeline->get_cut_start();
		const String seconds = duration < WORKBENCH_SECONDS_STEP ? "<" + workbench_seconds(WORKBENCH_SECONDS_STEP) : workbench_seconds(duration);
		const int64_t frame_count = timeline->get_selection_frame_count();
		if (frame_count > 0) {
			selection_summary->set_text(vformat(TTRN("%d frame (%s s)", "%d frames (%s s)", frame_count), frame_count, seconds));
		} else {
			selection_summary->set_text(vformat(TTR("%s s", "Animation Range"), seconds));
		}
	}
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

static Dictionary workbench_delete_clip_range(const Ref<Animation> &p_source, double p_first, double p_last, bool p_include_end, double p_frame_step = 0.0) {
	Dictionary result;
	result["error"] = ERR_INVALID_PARAMETER;
	if (p_source.is_null()) {
		result["message"] = TTR("No animation to edit.");
		return result;
	}
	const double length = p_source->get_length();
	if (!Math::is_finite(p_first) || !Math::is_finite(p_last) || p_first < 0 || p_last <= p_first || p_last > length + 0.000001) {
		result["message"] = vformat(TTR("The range %f - %f is outside the clip (0 - %f)."), p_first, p_last, length);
		return result;
	}
	if (p_last - p_first >= length - 0.000001) {
		result["message"] = TTR("The range covers the whole clip. Delete the clip file instead.");
		return result;
	}
	for (int track = 0; track < p_source->get_track_count(); track++) {
		if (p_source->track_is_compressed(track)) {
			result["error"] = ERR_UNAVAILABLE;
			result["message"] = vformat(TTR("Track %d (%s) is compressed. Clip editing requires uncompressed tracks."), track, String(p_source->track_get_path(track)));
			return result;
		}
	}
	Ref<Animation> clip = p_source->duplicate(true);
	const double removed = p_last - p_first;
	const bool tail = p_last >= length - 0.000001;
	const bool frame_cut = p_frame_step > 0;
	const bool inclusive_time = p_include_end && !frame_cut;
	const bool remove_terminal = p_include_end && tail;
	// Imported sample times can be stored at single precision. Match keys and markers near the frame boundary.
	const double tolerance = p_include_end ? MIN((frame_cut ? p_frame_step : removed) * 0.001, MAX(0.000001, p_last * 4 * FLT_EPSILON)) : 0.0;
	const double first_boundary = p_first - tolerance;
	const double last_boundary = p_last + (inclusive_time ? tolerance : -tolerance);
	const auto keep_after_range = [&](double p_time) {
		return !remove_terminal && (inclusive_time ? p_time > last_boundary : p_time >= last_boundary);
	};
	clip->set_length(length - removed);
	// Disabled tracks and every track type keep the same timing as the pose tracks.
	for (int track = 0; track < p_source->get_track_count(); track++) {
		for (int key = clip->track_get_key_count(track) - 1; key >= 0; key--) {
			clip->track_remove_key(track, key);
		}
		for (int key = 0; key < p_source->track_get_key_count(track); key++) {
			const double t = p_source->track_get_key_time(track, key);
			if (t < first_boundary) {
				clip->track_insert_key(track, t, p_source->track_get_key_value(track, key), p_source->track_get_key_transition(track, key));
			} else if (keep_after_range(t) && t <= length) {
				clip->track_insert_key(track, MAX(p_first, t - removed), p_source->track_get_key_value(track, key), p_source->track_get_key_transition(track, key));
			}
		}
		// Pose tracks hold the pose on both sides of the cut so the remaining motion is unchanged.
		if (workbench_pose_track(p_source->track_get_type(track))) {
			if (p_first > 0 && !tail) {
				workbench_boundary(p_source, clip, track, MAX(0.0, p_first - 0.00001), MAX(0.0, p_first - 0.00001));
			}
			double boundary_sample = MIN(length, p_last + (inclusive_time ? tolerance : 0.0));
			if (tail) {
				boundary_sample = remove_terminal ? MAX(0.0, p_first - (frame_cut ? p_frame_step : tolerance)) : p_first;
			}
			workbench_boundary(p_source, clip, track, p_first, boundary_sample);
		}
	}
	PackedStringArray removed_markers;
	for (const String &marker : p_source->get_marker_names()) {
		const double t = p_source->get_marker_time(marker);
		if (t < first_boundary) {
			continue;
		}
		const Color color = p_source->get_marker_color(marker);
		clip->remove_marker(marker);
		if (keep_after_range(t)) {
			clip->add_marker(marker, MAX(p_first, t - removed));
			clip->set_marker_color(marker, color);
		} else {
			removed_markers.push_back(marker);
		}
	}
	result["error"] = OK;
	result["message"] = String();
	result["animation"] = clip;
	result["removed_markers"] = removed_markers;
	return result;
}

Dictionary AnimationWorkbenchEditorPlugin::delete_clip_range(const Ref<Animation> &p_source, double p_first, double p_last) {
	return workbench_delete_clip_range(p_source, p_first, p_last, false);
}

Error AnimationWorkbenchEditorPlugin::delete_range(double p_first, double p_last) {
	return _delete_range(p_first, p_last, false);
}

Error AnimationWorkbenchEditorPlugin::_delete_range(double p_first, double p_last, bool p_include_end, double p_frame_step) {
	const Dictionary result = workbench_delete_clip_range(get_current_animation(), p_first, p_last, p_include_end, p_frame_step);
	last_error = result["message"];
	const Error error = Error(int(result["error"]));
	if (error != OK) {
		status->set_text(last_error);
		return error;
	}
	Draft &draft = drafts[current_clip];
	if (draft.saved > draft.cursor) {
		draft.saved = -1;
	}
	draft.states.resize(draft.cursor + 1);
	draft.states.push_back(result["animation"]);
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
	if (p_draft.source->get_path().has_extension("skanim")) {
		const String skeleton_path = p_draft.source->get_skeleton_path();
		if (skeleton_path.is_empty() || !skeleton_path.has_extension("sk") || p_draft.source->get_skeleton().is_null()) {
			last_error = TTR("A .skanim clip must be bound to a saved .sk skeleton before it can be saved.");
			status->set_text(last_error);
			return ERR_INVALID_DATA;
		}
	}
	if (EditorNode::get_singleton()->is_resource_read_only(p_draft.source, true)) {
		last_error = TTR("The animation belongs to an imported resource and cannot be overwritten.");
		status->set_text(last_error);
		return ERR_FILE_NO_PERMISSION;
	}
	Ref<Animation> before = p_draft.source->duplicate(true);
	p_draft.writing = true;
	p_draft.source->copy_from(p_draft.current());
	Error error;
	if (!p_draft.source->is_built_in()) {
		error = ResourceSaver::save(p_draft.source);
	} else if (p_draft.library.is_valid() && !p_draft.library->is_built_in()) {
		error = ResourceSaver::save(p_draft.library);
	} else {
		// A clip stored inside a scene is written by that scene's own save.
		Node *root = Object::cast_to<Node>(ObjectDB::get_instance(p_draft.scene));
		error = ERR_DOES_NOT_EXIST;
		EditorData &scene_data = EditorNode::get_editor_data();
		const String owner = p_draft.source->get_path().get_slice("::", 0);
		for (int scene = 0; root && scene < scene_data.get_edited_scene_count(); scene++) {
			if (scene_data.get_edited_scene_root(scene) == root && (owner.is_empty() || owner == root->get_scene_file_path())) {
				EditorUndoRedoManager::get_singleton()->set_history_as_unsaved(scene_data.get_scene_history_id(scene));
				error = OK;
			}
		}
	}
	if (error != OK) {
		p_draft.source->copy_from(before);
		p_draft.writing = false;
		last_error = vformat(TTR("Animation save failed: %s"), error_names[error]);
		status->set_text(last_error);
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
		LocalVector<StringName> libraries;
		definition->get_animation_library_list(&libraries);
		for (const StringName &library_name : libraries) {
			Ref<AnimationLibrary> library = definition->get_animation_library(library_name);
			LocalVector<StringName> names;
			library->get_animation_list(&names);
			for (const StringName &name : names) {
				if (library->get_animation(name) == p_clip) {
					return true;
				}
			}
		}
		return false;
	}
	AnimationPlayer *player = Object::cast_to<AnimationPlayer>(ObjectDB::get_instance(p_document.player));
	return player && player->find_animation(p_clip) != StringName();
}

void AnimationWorkbenchEditorPlugin::_update_document_status() {
	EditorSceneTabs *tabs = EditorSceneTabs::get_singleton();
	for (const KeyValue<String, Document> &entry : documents) {
		tabs->set_document_modified(entry.key, _document_dirty(entry.key));
		PackedStringArray unsaved_clips;
		for (const KeyValue<ObjectID, Draft> &draft : drafts) {
			if (draft.value.dirty() && _owns_clip(entry.value, draft.value.source)) {
				unsaved_clips.push_back(draft.value.source->get_path().get_file());
			}
		}
		String tooltip = entry.key;
		if (!unsaved_clips.is_empty()) {
			tooltip += "\n\n" + TTR("Unsaved animation clips:") + "\n" + String("\n").join(unsaved_clips);
		}
		tabs->set_document_tooltip(entry.key, tooltip);
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

AnimationPlayer *AnimationWorkbenchEditorPlugin::_get_catalog_player() const {
	return Object::cast_to<AnimationPlayer>(ObjectDB::get_instance(catalog_player_id));
}

void AnimationWorkbenchEditorPlugin::_catalog_changed() {
	AnimationPlayer *catalog_player = _get_catalog_player();
	if (!catalog_player) {
		return;
	}
	if (documents[current_document].player == catalog_player_id) {
		skeleton = catalog_player->get_skeleton_definition();
		_update_directory_row();
	}
	const Ref<Animation> &selection = catalog_player->get_animation_or_null(documents[current_document].animation);
	if (selection.is_valid() && selection->get_instance_id() == current_clip) {
		_filter(search->get_text());
	} else {
		_refresh();
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
	if (!drafts.has(p_clip)) {
		return; // The clip was deleted before this deferred refresh.
	}
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

Error AnimationWorkbenchEditorPlugin::save_clip_drafts(Node *p_scene) {
	bool changed = false;
	for (KeyValue<ObjectID, Draft> &entry : drafts) {
		Draft &draft = entry.value;
		if (!draft.dirty() || (p_scene && draft.scene != p_scene->get_instance_id())) {
			continue;
		}
		const Error error = _save_draft(draft);
		if (error != OK) {
			return error;
		}
		changed = true;
	}
	if (changed) {
		_update_document_status();
		_update_controls();
	}
	return OK;
}

void AnimationWorkbenchEditorPlugin::save_external_data() {
	if (!EditorNode::get_singleton()->is_saving_scene() && save_clip_drafts(nullptr) != OK) {
		EditorNode::get_singleton()->show_warning(last_error);
	}
}

String AnimationWorkbenchEditorPlugin::get_scene_unsaved_status(const Node *p_scene) const {
	for (const KeyValue<ObjectID, Draft> &entry : drafts) {
		if (entry.value.scene == p_scene->get_instance_id() && entry.value.dirty()) {
			return TTR("Animation workbench has unsaved clip edits.");
		}
	}
	return String();
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

void AnimationWorkbenchEditorPlugin::_save_as_requested() {
	if (!drafts.has(current_clip)) {
		return;
	}
	const String path = drafts[current_clip].source->get_path();
	save_as->set_current_path(path.get_base_dir().path_join(path.get_file().get_basename() + "_edited.skanim"));
	save_as->popup_file_dialog();
}

void AnimationWorkbenchEditorPlugin::_save_as_file(const String &p_path) {
	if (!drafts.has(current_clip) || skeleton.is_null() || FileAccess::exists(p_path)) {
		status->set_text(TTR("Choose a new clip filename and a skeleton."));
		return;
	}
	if (!_catalog_editable()) {
		status->set_text(last_error);
		return;
	}
	// The new clip must land where the skeleton looks for clips, or it would not be an animation.
	const String root = skeleton->get_animation_directory_path();
	const String directory = p_path.get_base_dir();
	StringName library_name;
	if (directory != root) {
		if (directory.get_base_dir() != root) {
			status->set_text(vformat(TTR("Save the clip inside '%s' or one of its library folders."), root));
			return;
		}
		library_name = directory.get_file();
	}
	const String animation_name = p_path.get_file().get_basename();
	if (!AnimationLibrary::is_valid_animation_name(animation_name) || (library_name != StringName() && !AnimationLibrary::is_valid_library_name(library_name))) {
		status->set_text(TTR("Enter a valid animation name without /, :, comma, or brackets."));
		return;
	}
	Ref<Animation> clip = drafts[current_clip].current()->duplicate(true);
	clip->set_skeleton(skeleton);
	String reason;
	if (!_clip_matches_skeleton(clip, skeleton, reason)) {
		status->set_text(reason);
		return;
	}
	const Error error = ResourceSaver::save(clip, p_path);
	if (error != OK) {
		status->set_text(error_names[error]);
		return;
	}
	clip->set_path(p_path, true);
	Draft &old = drafts[current_clip];
	old.states.clear();
	old.states.push_back(old.source->duplicate(true));
	old.cursor = old.saved = 0;
	_after_catalog_edit(join_animation_name(library_name, animation_name));
}

bool AnimationWorkbenchEditorPlugin::_document_dirty(const String &p_path) const {
	const Document *document = documents.getptr(p_path);
	if (!document) {
		return false;
	}
	for (const KeyValue<ObjectID, Draft> &draft : drafts) {
		if (draft.value.dirty() && _owns_clip(*document, draft.value.source)) {
			return true;
		}
	}
	return false;
}

void AnimationWorkbenchEditorPlugin::_navigate(const Callable &p_action) {
	// Drafts stay with their clips across tab and clip switches. Only closing asks.
	if (EditorSceneTabs::get_singleton()->is_closing_document() && _document_dirty(current_document)) {
		pending_navigation = p_action;
		navigation->popup_centered();
	} else {
		p_action.call();
	}
}

void AnimationWorkbenchEditorPlugin::_navigation_save() {
	_save_document(current_document);
	if (!_document_dirty(current_document)) {
		_continue_navigation();
	}
}

void AnimationWorkbenchEditorPlugin::_navigation_discard(const String &p_action) {
	const Document document = documents[current_document];
	for (KeyValue<ObjectID, Draft> &entry : drafts) {
		Draft &draft = entry.value;
		if (draft.dirty() && _owns_clip(document, draft.source)) {
			draft.states.clear();
			draft.states.push_back(draft.source->duplicate(true));
			draft.cursor = draft.saved = 0;
		}
	}
	if (drafts.has(current_clip) && model) {
		_render_draft();
	}
	_update_document_status();
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
		_update_model_picker_icon();
		set_name("AnimationWorkbench");

		connect("scene_closed", callable_mp(this, &AnimationWorkbenchEditorPlugin::_prune_scene_drafts).unbind(1), CONNECT_DEFERRED);
		get_tree()->connect("node_removed", callable_mp(this, &AnimationWorkbenchEditorPlugin::_node_removed));
		loop->set_button_icon(workspace->get_theme_icon(SNAME("Loop"), SNAME("EditorIcons")));
		reset->set_button_icon(workspace->get_theme_icon(SNAME("Reload"), SNAME("EditorIcons")));
		playing = true;
		play->set_button_icon(workspace->get_theme_icon(SNAME("Play"), SNAME("EditorIcons")));
		_update_controls();
		EditorFileSystem::get_singleton()->connect("filesystem_changed", callable_mp(this, &AnimationWorkbenchEditorPlugin::_filesystem_changed));
		add_tool_menu_item(TTR("Import Model / Animation..."), callable_mp(this, &AnimationWorkbenchEditorPlugin::_show_import).bind(String(), String("res://")));
	} else if (p_what == NOTIFICATION_PROCESS) {
		const real_t delta = get_process_delta_time();
		if (playing && visible && workspace->is_visible_in_tree() && drafts.has(current_clip)) {
			const double duration = drafts[current_clip].current()->get_length();
			double next = time + delta * speed;
			if (next >= duration) {
				if (loop->is_pressed()) {
					next = Math::fmod(next, duration);
				} else {
					next = duration;
					_pause();
				}
			}
			_sample(next);
		}
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		_clear_model();
		EditorInterface::get_singleton()->set_3d_asset_workspace(nullptr);
		memdelete(workspace);
		workspace = nullptr;
		for (const KeyValue<String, Document> &entry : documents) {
			EditorSceneTabs::get_singleton()->remove_document(entry.key, false);
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
	auto_skeleton = memnew(CheckBox(TTR("Match Automatically")));
	auto_skeleton->set_tooltip_text(TTR("Use the project skeleton that matches each file. Create a new skeleton when a model matches none."));
	auto_skeleton->set_button_group(group);
	bones->add_child(auto_skeleton);
	auto_skeleton->connect(SceneStringName(toggled), callable_mp(this, &AnimationWorkbenchEditorPlugin::_import_settings_changed));
	new_skeleton = memnew(CheckBox(TTR("Create Skeleton")));
	existing_skeleton = memnew(CheckBox(TTR("Use Existing Skeleton")));
	new_skeleton->set_button_group(group);
	existing_skeleton->set_button_group(group);
	bones->add_child(new_skeleton);
	bones->add_child(existing_skeleton);
	auto_skeleton->set_pressed_no_signal(true);
	new_skeleton->connect(SceneStringName(toggled), callable_mp(this, &AnimationWorkbenchEditorPlugin::_import_settings_changed));
	existing_skeleton->connect(SceneStringName(toggled), callable_mp(this, &AnimationWorkbenchEditorPlugin::_import_settings_changed));
	choose_skeleton = workbench_button(bones, TTR("Select"), callable_mp(this, &AnimationWorkbenchEditorPlugin::_browse_skeleton));
	skeleton_name = memnew(Label);
	bones->add_child(skeleton_name);
	HBoxContainer *fps = memnew(HBoxContainer);
	import_options->add_child(fps);
	fps->add_child(memnew(Label(TTR("Import Sampling FPS"))));
	auto_sampling = memnew(CheckBox(TTR("Automatic")));
	auto_sampling->set_pressed(true);
	auto_sampling->set_tooltip_text(TTR("Detect the sampling rate of each animation from its keyframe times. Use 30 FPS when no rate can be detected."));
	fps->add_child(auto_sampling);
	auto_sampling->connect(SceneStringName(toggled), callable_mp(this, &AnimationWorkbenchEditorPlugin::_import_settings_changed));
	sampling = memnew(SpinBox);
	sampling->set_min(1);
	sampling->set_max(240);
	sampling->set_value(30);
	fps->add_child(sampling);
	import_status = memnew(Label);
	import_status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	layout->add_child(import_status);
	import_status->set_custom_minimum_size(Size2(0, 60 * EDSCALE));
	import_dialog->add_button(TTR("Copy as Native GLB"), true, "native");
	import_dialog->connect("custom_action", callable_mp(this, &AnimationWorkbenchEditorPlugin::_import_native));
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
	pending_sources.clear();
	if (!p_source.is_empty()) {
		pending_sources.push_back(p_source);
	}
	_open_import(p_source, p_destination);
}

void AnimationWorkbenchEditorPlugin::_open_import(const String &p_source, const String &p_destination) {
	updating_mesh = false;
	import_dialog->set_title(TTR("Import Model / Animation"));
	import_dialog->set_ok_button_text(TTR("Import Assets"));
	source_path->set_text(pending_sources.size() > 1 ? vformat(TTR("%d files: %s"), pending_sources.size(), String(", ").join(pending_sources)) : p_source);
	source_path->set_editable(pending_sources.size() <= 1);
	destination_path->set_text(p_destination);
	destination_row->show();
	target_row->hide();
	import_options->show();
	import_status->set_text(String());
	_import_settings_changed(false);
	import_dialog->popup_centered(Size2i(700, 0) * EDSCALE);
}

void AnimationWorkbenchEditorPlugin::_show_update(const String &p_path) {
	_open_update(p_path);
}

void AnimationWorkbenchEditorPlugin::_open_update(const String &p_path) {
	updating_mesh = true;
	import_dialog->set_title(TTR("Update SKMesh"));
	import_dialog->set_ok_button_text(TTR("Update SKMesh"));
	pending_sources.clear();
	source_path->clear();
	source_path->set_editable(true);
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
	if (only_animations && new_skeleton->is_pressed()) {
		auto_skeleton->set_pressed(true);
	}
	choose_skeleton->set_disabled(!existing_skeleton->is_pressed());
	skeleton_name->set_text(existing_skeleton->is_pressed() && import_skeleton.is_valid() ? import_skeleton->get_path().get_file() : String());
	auto_sampling->set_disabled(!import_animations->is_pressed());
	sampling->set_visible(!auto_sampling->is_pressed());
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

Dictionary AnimationWorkbenchEditorPlugin::import_files(const PackedStringArray &p_sources, const String &p_destination, const Dictionary &p_settings) {
	Array results;
	PackedStringArray failures;
	String first_model;
	String first_clip;
	String first_scene;
	for (const String &source : p_sources) {
		Dictionary result = EditorInterface::get_singleton()->import_animation_assets(source, p_destination, p_settings);
		result["source"] = source;
		results.push_back(result);
		if (int(result.get("error", FAILED)) != OK) {
			failures.push_back(source.get_file() + ": " + String(result.get("message", "Import failed.")));
			continue;
		}
		const Dictionary import_metadata = result.get("metadata", Dictionary());
		const Array reports = import_metadata.get("skeleton_validation", Array());
		for (const Variant &value : reports) {
			const Dictionary report = value;
			const PackedStringArray clips = report.get("animations", PackedStringArray());
			if (first_model.is_empty()) {
				first_model = report.get("model", String());
			}
			if (first_clip.is_empty() && !clips.is_empty()) {
				first_clip = clips[0];
			}
		}
		if (first_scene.is_empty()) {
			first_scene = import_metadata.get("scene", String());
		}
	}
	Dictionary summary;
	summary["error"] = failures.is_empty() ? OK : FAILED;
	summary["message"] = String("\n").join(failures);
	summary["results"] = results;
	summary["model"] = first_model;
	summary["clip"] = first_clip;
	summary["scene"] = first_scene;
	return summary;
}

void AnimationWorkbenchEditorPlugin::_import_confirmed() {
	Dictionary summary;
	if (updating_mesh) {
		const Dictionary result = EditorInterface::get_singleton()->update_skeletal_mesh(source_path->get_text(), update_path->get_text());
		if (int(result.get("error", FAILED)) != OK) {
			import_status->set_text(result.get("message", TTR("Import failed.")));
			return;
		}
		summary["model"] = update_path->get_text();
	} else {
		if (existing_skeleton->is_pressed() && import_skeleton.is_null()) {
			import_status->set_text(TTR("Choose an existing skeleton, or match automatically."));
			return;
		}
		Dictionary settings;
		settings["animation_only"] = !import_model->is_pressed();
		settings["import_animations"] = import_animations->is_pressed();
		settings["skeleton"] = existing_skeleton->is_pressed() ? import_skeleton->get_path() : String();
		settings["create_skeleton"] = new_skeleton->is_pressed();
		settings["fps"] = auto_sampling->is_pressed() ? 0.0 : sampling->get_value();
		PackedStringArray sources = pending_sources;
		if (sources.size() <= 1) {
			sources.clear();
			sources.push_back(source_path->get_text());
		}
		summary = import_files(sources, destination_path->get_text(), settings);
		if (int(summary["error"]) != OK) {
			// Files that imported stay imported. The dialog keeps only the failures for another attempt.
			PackedStringArray failed;
			for (const Variant &value : Array(summary["results"])) {
				const Dictionary result = value;
				if (int(result.get("error", FAILED)) != OK) {
					failed.push_back(result["source"]);
				}
			}
			pending_sources = failed;
			source_path->set_text(failed.size() > 1 ? vformat(TTR("%d files: %s"), failed.size(), String(", ").join(failed)) : failed[0]);
			source_path->set_editable(failed.size() <= 1);
			import_status->set_text(summary["message"]);
			return;
		}
	}
	import_dialog->hide();
	pending_sources.clear();
	const String mesh = summary.get("model", String());
	const String clip = summary.get("clip", String());
	const String scene = summary.get("scene", String());
	if (!mesh.is_empty()) {
		if (EditorInterface::get_singleton()->get_open_scenes().has(mesh)) {
			EditorInterface::get_singleton()->reload_scene_from_path(mesh);
		}
		EditorInterface::get_singleton()->open_scene_from_path(mesh);
	} else if (!clip.is_empty()) {
		open_resource(ResourceLoader::load(clip));
	} else if (!scene.is_empty()) {
		EditorInterface::get_singleton()->open_scene_from_path(scene);
	}
}

void AnimationWorkbenchEditorPlugin::_import_native(const String &p_action) {
	if (p_action != "native" || updating_mesh) {
		return;
	}
	// Keeps the native importer for static models: the source is copied and stays reimportable.
	PackedStringArray sources = pending_sources;
	if (sources.size() <= 1) {
		sources.clear();
		sources.push_back(source_path->get_text());
	}
	const String folder = destination_path->get_text();
	DirAccess::make_dir_recursive_absolute(folder);
	for (const String &source : sources) {
		const Error error = DirAccess::copy_absolute(source, ProjectSettings::get_singleton()->globalize_path(folder.path_join(source.get_file())));
		if (error != OK) {
			import_status->set_text(vformat(TTR("Cannot copy %s: %s"), source, error_names[error]));
			return;
		}
	}
	import_dialog->hide();
	pending_sources.clear();
	EditorFileSystem::get_singleton()->scan_changes();
}

void AnimationWorkbenchEditorPlugin::import_dropped_files(const PackedStringArray &p_files, const String &p_folder) {
	pending_sources = p_files;
	_open_import(p_files.is_empty() ? String() : p_files[0], p_folder);
}

void AnimationWorkbenchEditorPlugin::request_mesh_update(const String &p_path) {
	_open_update(p_path);
}

void AnimationWorkbenchEditorPlugin::_cancel_import() {
	pending_sources.clear();
}

void AnimationWorkbenchEditorPlugin::_speed_selected(int p_index) {
	static const double speeds[] = { 0.25, 0.5, 1.0, 2.0 };
	speed = speeds[p_index];
}

void AnimationWorkbenchEditorPlugin::_delete_selected() {
	if (timeline->has_selection()) {
		const double fps = timeline->get_sample_rate();
		_delete_range(timeline->get_cut_start(), timeline->get_cut_end(), true, fps > 0 ? 1.0 / fps : 0.0);
	}
}

void AnimationWorkbenchEditorPlugin::_navigation_cancel() {
	pending_navigation = Callable();
}

String AnimationWorkbenchEditorPlugin::_find_model(const Ref<SkeletonDefinition> &p_skeleton) const {
	const Vector<String> models = _matching_models(p_skeleton);
	return models.is_empty() ? String() : models[0];
}

Error AnimationWorkbenchEditorPlugin::set_loop_mode(int p_mode) {
	if (!drafts.has(current_clip) || p_mode < Animation::LOOP_NONE || p_mode > Animation::LOOP_PINGPONG) {
		return ERR_INVALID_PARAMETER;
	}
	Draft &draft = drafts[current_clip];
	if (int(draft.current()->get_loop_mode()) == p_mode) {
		return OK;
	}
	Ref<Animation> clip = draft.current()->duplicate(true);
	clip->set_loop_mode(Animation::LoopMode(p_mode));
	if (draft.saved > draft.cursor) {
		draft.saved = -1;
	}
	draft.states.resize(draft.cursor + 1);
	draft.states.push_back(clip);
	draft.cursor++;
	_render_draft();
	return OK;
}

void AnimationWorkbenchEditorPlugin::set_preview_loop(bool p_enabled) {
	if (loop) {
		loop->set_pressed_no_signal(p_enabled);
	}
}

bool AnimationWorkbenchEditorPlugin::is_preview_loop_enabled() const {
	return loop && loop->is_pressed();
}

void AnimationWorkbenchEditorPlugin::set_preview_speed(double p_speed) {
	static const double speeds[] = { 0.25, 0.5, 1.0, 2.0 };
	int closest = 2;
	for (int i = 0; i < 4; i++) {
		if (Math::abs(speeds[i] - p_speed) < Math::abs(speeds[closest] - p_speed)) {
			closest = i;
		}
	}
	speed = speeds[closest];
	if (rate) {
		rate->select(closest);
	}
}

void AnimationWorkbenchEditorPlugin::set_playing(bool p_playing) {
	_play_toggled(p_playing);
}

void AnimationWorkbenchEditorPlugin::seek(double p_time) {
	_pause();
	_sample(p_time);
}

void AnimationWorkbenchEditorPlugin::set_selection(double p_first, double p_last) {
	_set_selection(p_first, p_last);
}

Dictionary AnimationWorkbenchEditorPlugin::get_workbench_state() const {
	Dictionary state;
	state["document"] = current_document;
	state["has_preview_model"] = model != nullptr;
	state["last_error"] = last_error;
	state["playing"] = playing;
	state["preview_loop"] = is_preview_loop_enabled();
	state["preview_speed"] = speed;
	state["preview_model"] = skeleton.is_valid() ? skeleton->get_preview_mesh() : String();
	state["rebind_pending"] = rebind_dialog && rebind_dialog->is_visible();
	state["legacy_catalog"] = skeleton.is_valid() && skeleton->has_baked_animation_libraries();
	state["animation_directory"] = skeleton.is_valid() ? skeleton->get_animation_directory_path() : String();
	state["scan_issues"] = skeleton.is_valid() ? skeleton->get_animation_scan_issues() : TypedArray<Dictionary>();
	state["rebind_clip"] = rebind_clip.is_valid() ? rebind_clip->get_path() : String();
	state["rebind_skeleton"] = rebind_skeleton.is_valid() ? rebind_skeleton->get_path() : String();
	PackedStringArray preview_candidates;
	for (const String &path : _matching_models(skeleton)) {
		preview_candidates.push_back(path);
	}
	state["preview_model_candidates"] = preview_candidates;
	state["time"] = time;
	state["selection_start"] = timeline->get_selection().x;
	state["selection_end"] = timeline->get_selection().y;
	const Document *document = documents.getptr(current_document);
	state["animation"] = document ? document->animation : StringName();
	state["document_modified"] = _document_dirty(current_document);
	const Draft *draft = drafts.getptr(current_clip);
	state["clip_path"] = draft ? draft->source->get_path() : String();
	state["clip_modified"] = draft && draft->dirty();
	state["length"] = draft ? draft->current()->get_length() : 0.0;
	state["loop_mode"] = draft ? int(draft->current()->get_loop_mode()) : 0;
	state["undo_steps"] = draft ? draft->cursor : 0;
	state["redo_steps"] = draft ? draft->states.size() - 1 - draft->cursor : 0;
	PackedStringArray open_documents;
	for (const KeyValue<String, Document> &entry : documents) {
		open_documents.push_back(entry.key);
	}
	state["open_documents"] = open_documents;
	return state;
}
