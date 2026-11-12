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

#include "editor/plugins/editor_plugin.h"
#include "scene/gui/range.h"
#include "scene/resources/animation.h"
#include "scene/resources/skeleton_definition.h"

class AnimationPlayer;
class Button;
class Camera3D;
class CheckBox;
class CheckButton;
class ConfirmationDialog;
class EditorFileDialog;
class EditorResourcePicker;
class Label;
class LineEdit;
class Node3D;
class OptionButton;
class PackedScene;
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
	Vector2 selection;
	Vector<double> marks;
	bool frames = false;
	bool editing = true;
	enum Drag { NONE,
		HEAD,
		SELECTION,
		PAN };
	Drag drag = NONE;
	double anchor = 0.0;
	double pan_start = 0.0;
	double _time_at(double p_x) const;
	double _x_at(double p_time) const;
	void _move(double p_x);

protected:
	static void _bind_methods();
	void _notification(int p_what);
	void gui_input(const Ref<InputEvent> &p_event) override;

public:
	void set_clip(const Ref<Animation> &p_clip, double p_rate);
	void set_selection(double p_first, double p_last);
	Vector2 get_selection() const { return selection; }
	void set_frames(bool p_frames) {
		frames = p_frames;
		queue_redraw();
	}
	void set_editing(bool p_editing) {
		editing = p_editing;
		queue_redraw();
	}
	double snap(double p_time) const;
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
	AnimationPlayer *catalog_player = nullptr;
	AnimationPlayer *sample_player = nullptr;
	Vector<Pose> poses;
	Vector3 center = Vector3(0, 1, 0);
	Vector2 orbit = Vector2(-0.4, -0.12);
	double distance = 3.0;
	double time = 0.0;
	double speed = 1.0;
	bool playing = false;
	bool updating = false;
	bool saving = false;
	bool visible = false;
	Callable pending_navigation;
	VBoxContainer *workspace = nullptr;
	SubViewportContainer *view = nullptr;
	SubViewport *viewport = nullptr;
	Node3D *world = nullptr;
	Camera3D *camera = nullptr;
	EditorResourcePicker *model_picker = nullptr;
	LineEdit *search = nullptr;
	Tree *list = nullptr;
	Label *title = nullptr;
	Label *clip_title = nullptr;
	Label *clock = nullptr;
	Label *status = nullptr;
	AnimationClipTimeline *timeline = nullptr;
	Button *play = nullptr;
	Button *loop = nullptr;
	Button *undo_button = nullptr;
	Button *redo_button = nullptr;
	Button *delete_button = nullptr;
	Button *save_button = nullptr;
	OptionButton *units = nullptr;
	SpinBox *first = nullptr;
	SpinBox *last = nullptr;
	ConfirmationDialog *navigation = nullptr;
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
	SpinBox *sampling = nullptr;
	Ref<SkeletonDefinition> import_skeleton;
	EditorFileDialog *source_files = nullptr;
	EditorFileDialog *target_folder = nullptr;
	VBoxContainer *destination_row = nullptr;
	VBoxContainer *target_row = nullptr;
	VBoxContainer *import_options = nullptr;
	bool updating_mesh = false;
	Vector<Pair<String, String>> dropped_files;
	void _register_document(const String &p_path, const Document &p_document, bool p_activate);
	void _activate_document(const String &p_path);
	void _inspect_document(const String &p_path);
	void _close_document(const String &p_path);
	void _save_document(const String &p_path);
	void _restore_document(const String &p_path);
	void _document_selected(const String &p_path);
	void _leave_document();
	void _scene_closed(const String &p_path);
	void _node_removed(Node *p_node);
	void _prune_documents();
	void _clear_model();
	void _build_preview(Node3D *p_model, AnimationPlayer *p_catalog);
	void _set_model(const Ref<Resource> &p_model);
	void _preview_input(const Ref<InputEvent> &p_event);
	void _frame_model();
	void _update_camera();
	void _reset_pose();
	void _filter(const String &p_query);
	void _list_selected();
	void _select(const StringName &p_name, bool p_play);
	void _render_draft();
	void _refresh();
	void _catalog_changed();
	void _source_changed(ObjectID p_clip);
	void _refresh_source(ObjectID p_clip);
	void _sample(double p_time);
	void _pause();
	void _play_toggled(bool p_play);
	void _speed_selected(int p_index);
	void _delete_selected();
	void _navigation_cancel();
	void _apply_model(const Ref<PackedScene> &p_model);
	void _open_import(const String &p_source, const String &p_destination);
	void _open_update(const String &p_path);
	void _set_selection(double p_first, double p_last);
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
	void _build_import_dialog();
	void _show_import(const String &p_source, const String &p_destination);
	void _show_update(const String &p_path);
	void _import_settings_changed(bool p_pressed);
	void _browse_skeleton();
	void _skeleton_chosen(const String &p_path);
	void _import_confirmed();
	void _files_dropped(const PackedStringArray &p_files, const String &p_folder);
	void _next_drop();
	void _cancel_import();
	void _browse_source();
	void _browse_destination();
	static Ref<SkeletonDefinition> _model_skeleton(const Ref<PackedScene> &p_scene);

protected:
	static void _bind_methods();
	void _notification(int p_what);
	void input(const Ref<InputEvent> &p_event) override;

public:
	String get_plugin_name() const override { return "AnimationWorkbench"; }
	bool handles(Object *p_object) const override;
	void edit(Object *p_object) override;
	void make_visible(bool p_visible) override {}
	String get_unsaved_status(const String &p_scene = "") const override;
	void save_external_data() override;
	void open_player(AnimationPlayer *p_player);
	void open_resource(const Ref<Resource> &p_resource);
	void select_animation(const StringName &p_name);
	Error delete_range(double p_first, double p_last);
	Error save_animation();
	void undo_edit();
	void redo_edit();
	Ref<Animation> get_current_animation() const;
	AnimationWorkbenchEditorPlugin();
};
