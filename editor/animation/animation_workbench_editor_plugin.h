/**************************************************************************/
/*  animation_workbench_editor_plugin.h                                   */
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

#pragma once

#include "core/templates/hash_set.h"
#include "editor/plugins/editor_plugin.h"
#include "scene/gui/range.h"
#include "scene/resources/animation.h"
#include "scene/resources/skeleton_definition.h"

class AnimationPlayer;
class AcceptDialog;
class Button;
class Camera3D;
class CheckBox;
class ConfirmationDialog;
class EditorFileDialog;
class Label;
class LineEdit;
class MenuButton;
class Node3D;
class OptionButton;
class PackedScene;
class PanelContainer;
class PopupMenu;
class SpinBox;
class SubViewport;
class SubViewportContainer;
class Tree;
class VBoxContainer;

class AnimationClipTimeline : public Range {
	GDCLASS(AnimationClipTimeline, Range);
	double duration = 0.0;
	double rate = 0.0;
	double view_start = 0.0;
	double view_span = 1.0;
	double selection_start = 0.0;
	double selection_end = 0.0;
	bool selection_active = false;
	Vector<double> marks;
	bool frames = false;
	enum Drag { NONE,
		HEAD,
		SELECTION,
		PAN };
	Drag drag = NONE;
	double anchor = 0.0;
	double pan_start = 0.0;
	Rect2 _get_selection_lane() const;
	double _time_at(double p_x) const;
	double _x_at(double p_time) const;
	double _selection_frame_at(double p_time) const;
	void _clear_selection();
	void _move(double p_x);

protected:
	static void _bind_methods();
	void _notification(int p_what);
	void gui_input(const Ref<InputEvent> &p_event) override;
	CursorShape get_cursor_shape(const Point2 &p_pos) const override;

public:
	void set_clip(const Ref<Animation> &p_clip, double p_rate);
	void set_selection(double p_first, double p_last);
	Vector2 get_selection() const { return Vector2(selection_start, selection_end); }
	bool has_selection_endpoints() const { return selection_active; }
	bool has_selection() const;
	int64_t get_selection_frame_count() const;
	double get_cut_start() const;
	double get_cut_end() const;
	double get_sample_rate() const { return rate; }
	int64_t get_last_frame() const;
	bool is_frame_mode() const { return frames; }
	void set_frames(bool p_frames);
	double snap(double p_time) const;
	double snap_selection(double p_time) const;
	void fit();
	AnimationClipTimeline();
};

class AnimationWorkbenchEditorPlugin : public EditorPlugin {
	GDCLASS(AnimationWorkbenchEditorPlugin, EditorPlugin);
	struct Document {
		Ref<Resource> resource;
		ObjectID player;
		ObjectID scene;
		StringName animation;
	};
	struct Draft {
		Ref<Animation> source;
		Ref<AnimationLibrary> library;
		Vector<Ref<Animation>> states;
		int cursor = 0;
		int saved = 0;
		ObjectID scene;
		bool writing = false;
		bool refresh_queued = false;
		bool dirty() const { return cursor != saved; }
		Ref<Animation> current() const { return states[cursor]; }
	};
	struct Pose {
		Node3D *node = nullptr;
		Transform3D transform;
		Vector<Transform3D> bones;
		Vector<float> shapes;
	};
	HashMap<String, Document> documents;
	HashMap<ObjectID, Draft> drafts;
	String current_document;
	ObjectID current_clip;
	Ref<SkeletonDefinition> skeleton;
	Node3D *model = nullptr;
	ObjectID catalog_player_id;
	AnimationPlayer *sample_player = nullptr;
	Vector<Pose> poses;
	Vector3 center = Vector3(0, 1, 0);
	Vector2 orbit = Vector2(-0.4, -0.12);
	double distance = 3.0;
	double time = 0.0;
	double speed = 1.0;
	bool playing = false;
	bool prefer_frames = true;
	bool updating = false;
	bool saving = false;
	bool visible = false;
	Callable pending_navigation;
	VBoxContainer *workspace = nullptr;
	SubViewportContainer *view = nullptr;
	SubViewport *viewport = nullptr;
	Node3D *world = nullptr;
	Camera3D *camera = nullptr;
	Control *axis_gizmo = nullptr;
	void _draw_axis_gizmo();
	PanelContainer *model_settings = nullptr;
	Label *model_value = nullptr;
	MenuButton *model_picker = nullptr;
	Button *model_reveal = nullptr;
	LineEdit *search = nullptr;
	Tree *list = nullptr;
	Label *title = nullptr;
	Label *clip_title = nullptr;
	Label *seconds_readout = nullptr;
	Label *frames_readout = nullptr;
	Label *status = nullptr;
	AnimationClipTimeline *timeline = nullptr;
	Button *play = nullptr;
	Button *loop = nullptr;
	CheckBox *clip_loop_options[Animation::LOOP_PINGPONG + 1] = {};
	Button *reset = nullptr;
	OptionButton *rate = nullptr;
	Button *undo_button = nullptr;
	Button *redo_button = nullptr;
	Button *delete_button = nullptr;
	Button *save_button = nullptr;
	OptionButton *units = nullptr;
	SpinBox *first = nullptr;
	SpinBox *last = nullptr;
	Label *selection_summary = nullptr;
	ConfirmationDialog *navigation = nullptr;
	ConfirmationDialog *rebind_dialog = nullptr;
	Label *rebind_clip_path = nullptr;
	Label *rebind_skeleton_path = nullptr;
	LineEdit *rebind_animation_name = nullptr;
	Label *rebind_status = nullptr;
	Button *rebind_choose_skeleton = nullptr;
	EditorFileDialog *rebind_skeleton_picker = nullptr;
	Ref<Animation> rebind_clip;
	Ref<SkeletonDefinition> rebind_skeleton;
	enum ListAction {
		LIST_RENAME,
		LIST_DELETE,
		LIST_SHOW,
		LIST_NEW_LIBRARY,
		LIST_REBIND,
		LIST_MOVE_BASE = 100,
	};
	PanelContainer *directory_settings = nullptr;
	Label *directory_value = nullptr;
	Button *directory_reveal = nullptr;
	EditorFileDialog *directory_picker = nullptr;
	PopupMenu *list_menu = nullptr;
	ConfirmationDialog *delete_dialog = nullptr;
	ConfirmationDialog *migrate_dialog = nullptr;
	String list_target;
	String rename_target;
	Vector<StringName> menu_libraries;
	EditorFileDialog *save_as = nullptr;
	ConfirmationDialog *import_dialog = nullptr;
	LineEdit *source_path = nullptr;
	LineEdit *destination_path = nullptr;
	LineEdit *update_path = nullptr;
	CheckBox *import_model = nullptr;
	CheckBox *import_animations = nullptr;
	CheckBox *new_skeleton = nullptr;
	CheckBox *existing_skeleton = nullptr;
	Button *choose_skeleton = nullptr;
	Label *skeleton_name = nullptr;
	Label *import_status = nullptr;
	CheckBox *auto_sampling = nullptr;
	SpinBox *sampling = nullptr;
	Ref<SkeletonDefinition> import_skeleton;
	EditorFileDialog *source_files = nullptr;
	EditorFileDialog *target_folder = nullptr;
	VBoxContainer *destination_row = nullptr;
	VBoxContainer *target_row = nullptr;
	VBoxContainer *import_options = nullptr;
	bool updating_mesh = false;
	PackedStringArray pending_sources;
	CheckBox *auto_skeleton = nullptr;
	String last_error;
	static AnimationWorkbenchEditorPlugin *singleton;
	bool _document_dirty(const String &p_path) const;
	String _find_model(const Ref<SkeletonDefinition> &p_skeleton) const;
	void _import_native(const String &p_action);
	void _register_document(const String &p_path, const Document &p_document, bool p_activate);
	bool _open_owned_clip(const Ref<Animation> &p_clip, bool p_activate);
	StringName _find_catalog_name(const Ref<SkeletonDefinition> &p_skeleton, const Ref<Animation> &p_clip) const;
	bool _clip_matches_skeleton(const Ref<Animation> &p_clip, const Ref<SkeletonDefinition> &p_skeleton, String &r_reason) const;
	bool _can_rebind(String &r_reason) const;
	void _open_rebind_dialog(const Ref<Animation> &p_clip, const Ref<SkeletonDefinition> &p_skeleton = Ref<SkeletonDefinition>());
	void _browse_rebind_skeleton();
	void _rebind_skeleton_selected(const String &p_path);
	void _rebind_name_changed(const String &p_name);
	void _rebind_confirmed();
	void _rebind_cancelled();
	void _activate_document(const String &p_path);
	void _inspect_document(const String &p_path);
	void _close_document(const String &p_path);
	void _save_document(const String &p_path);
	void _restore_document(const String &p_path);
	void _document_selected(const String &p_path);
	void _leave_document();
	void _node_removed(Node *p_node);
	void _prune_documents();
	void _prune_scene_drafts();
	void _clear_model();
	void _build_preview(Node3D *p_model, AnimationPlayer *p_catalog);
	Vector<String> _matching_models(const Ref<SkeletonDefinition> &p_skeleton) const;
	void _refresh_model_menu();
	void _model_selected(int p_id);
	void _update_model_picker();
	void _update_model_picker_icon();
	void _show_preview_model();
	void _preview_input(const Ref<InputEvent> &p_event);
	void _frame_model();
	void _update_camera();
	void _reset_preview();
	void _restore_pose();
	void _filter(const String &p_query);
	void _list_selected();
	void _select(const StringName &p_name, bool p_play);
	void _render_draft();
	void _refresh();
	void _catalog_changed();
	AnimationPlayer *_get_catalog_player() const;
	void _source_changed(ObjectID p_clip);
	void _refresh_source(ObjectID p_clip);
	void _sample(double p_time);
	void _update_readout_widths();
	void _update_readout_emphasis();
	void _pause();
	void _play_toggled(bool p_play);
	void _speed_selected(int p_index);
	void _delete_selected();
	void _navigation_cancel();
	void _open_import(const String &p_source, const String &p_destination);
	void _open_update(const String &p_path);
	void _set_selection(double p_first, double p_last);
	void _update_selection_fields(const Vector2 &p_selection);
	Error _delete_range(double p_first, double p_last, bool p_include_end, double p_frame_step = 0.0);
	void _range_changed(double p_value);
	void _units_changed(int p_index);
	void _update_controls();
	void _update_document_status();
	bool _owns_clip(const Document &p_document, const Ref<Animation> &p_clip) const;
	Error _save_draft(Draft &p_draft);
	void _save_as_requested();
	void _save_as_file(const String &p_path);
	void _navigate(const Callable &p_action);
	void _navigation_save();
	void _navigation_discard(const String &p_action);
	void _continue_navigation();
	void _list_clicked(const Vector2 &p_position, MouseButton p_button);
	void _list_empty_clicked(const Vector2 &p_position, MouseButton p_button);
	void _popup_list_menu(const Vector2 &p_position);
	void _list_menu_pressed(int p_id);
	void _list_activated();
	void _list_gui_input(const Ref<InputEvent> &p_event);
	void _begin_inline_rename();
	void _list_item_edited();
	void _apply_inline_rename(const String &p_target, const String &p_text);
	void _new_library_inline();
	void _request_delete(const StringName &p_name);
	Variant _list_get_drag_data(const Point2 &p_point, Control *p_from);
	bool _list_can_drop_data(const Point2 &p_point, const Variant &p_data, Control *p_from) const;
	void _list_drop_data(const Point2 &p_point, const Variant &p_data, Control *p_from);
	bool _drop_library(const Point2 &p_point, const Variant &p_data, StringName &r_library) const;
	void _delete_confirmed();
	void _migrate_confirmed();
	void _filesystem_changed();
	void _update_directory_row();
	void _browse_animation_directory();
	void _animation_directory_selected(const String &p_path);
	void _show_animation_directory();
	void _rescan();
	void _after_catalog_edit(const StringName &p_select);
	bool _catalog_editable();
	Ref<Animation> _catalog_clip(const StringName &p_name) const;
	bool _rebind_target(const Ref<Animation> &p_clip, const Ref<SkeletonDefinition> &p_skeleton, const String &p_name, String &r_target, String &r_reason) const;
	static Error _move_clip_file(const Ref<Animation> &p_clip, const String &p_path);
	void _build_import_dialog();
	void _show_import(const String &p_source, const String &p_destination);
	void _show_update(const String &p_path);
	void _import_settings_changed(bool p_pressed);
	void _browse_skeleton();
	void _skeleton_chosen(const String &p_path);
	void _import_confirmed();
	void _cancel_import();
	void _browse_source();
	void _browse_destination();
	bool _load_preview_model(const Ref<PackedScene> &p_scene, bool p_save_choice);

protected:
	static void _bind_methods();
	void _notification(int p_what);
	void input(const Ref<InputEvent> &p_event) override;

public:
	String get_plugin_name() const override { return "AnimationWorkbench"; }
	bool handles(Object *p_object) const override;
	void edit(Object *p_object) override;
	void make_visible(bool p_visible) override {}
	Error save_clip_drafts(Node *p_scene);
	void save_external_data() override;
	String get_scene_unsaved_status(const Node *p_scene) const override;
	String get_unsaved_status(const String &p_scene = "") const override;
	void open_player(AnimationPlayer *p_player);
	void open_resource(const Ref<Resource> &p_resource);
	Error rebind_animation(const Ref<Animation> &p_clip, const Ref<SkeletonDefinition> &p_skeleton, const StringName &p_animation_name);
	void select_animation(const StringName &p_name);
	Error rename_animation(const StringName &p_name, const String &p_new_name);
	Error move_animation(const StringName &p_name, const StringName &p_library);
	Error delete_animation(const StringName &p_name);
	Error create_animation_library(const String &p_library);
	Error set_animation_directory(const String &p_path);
	Error rename_animation_library(const StringName &p_library, const String &p_new_name);
	Error migrate_legacy_animations(const Ref<SkeletonDefinition> &p_skeleton);
	static Error migrate_skeleton_animations(const Ref<SkeletonDefinition> &p_skeleton, String &r_error);
	static AnimationWorkbenchEditorPlugin *get_singleton() { return singleton; }
	static Dictionary delete_clip_range(const Ref<Animation> &p_source, double p_first, double p_last);
	Error delete_range(double p_first, double p_last);
	Error set_loop_mode(int p_mode);
	void set_preview_loop(bool p_enabled);
	bool is_preview_loop_enabled() const;
	void set_preview_speed(double p_speed);
	void set_playing(bool p_playing);
	void seek(double p_time);
	void set_selection(double p_first, double p_last);
	String get_last_error() const { return last_error; }
	Dictionary get_workbench_state() const;
	Dictionary import_files(const PackedStringArray &p_sources, const String &p_destination, const Dictionary &p_settings = Dictionary());
	void import_dropped_files(const PackedStringArray &p_files, const String &p_folder);
	void request_mesh_update(const String &p_path);
	Error save_animation();
	void undo_edit();
	void redo_edit();
	Ref<Animation> get_current_animation() const;
	AnimationWorkbenchEditorPlugin();
};
