/**************************************************************************/
/*  animation_tree_editor_plugin.h                                        */
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

#include "editor/docks/editor_dock.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/plugins/editor_plugin.h"
#include "scene/animation/animation_tree.h"
#include "scene/gui/box_container.h"

class Button;
class EditorFileDialog;
class EditorResourcePicker;
class ConfirmationDialog;
class Label;
class SkeletonDefinition;
class Node3D;
class AnimationPlayer;
class PanelContainer;
class ScrollContainer;
class RichTextLabel;
class AnimationTreeEditorPlugin;

class AnimationGraphInspectorPlugin : public EditorInspectorPlugin {
	GDCLASS(AnimationGraphInspectorPlugin, EditorInspectorPlugin);
	AnimationTreeEditorPlugin *editor = nullptr;

public:
	bool can_handle(Object *p_object) override;
	void parse_begin(Object *p_object) override;
	AnimationGraphInspectorPlugin(AnimationTreeEditorPlugin *p_editor) { editor = p_editor; }
};

class AnimationTreeNodeEditorPlugin : public VBoxContainer {
	GDCLASS(AnimationTreeNodeEditorPlugin, VBoxContainer);

public:
	virtual bool can_edit(const Ref<AnimationNode> &p_node) = 0;
	virtual void edit(const Ref<AnimationNode> &p_node) = 0;

private:
	String last_error_key;
};

class AnimationTreeEditor : public EditorDock {
	GDCLASS(AnimationTreeEditor, EditorDock);

	ScrollContainer *path_edit = nullptr;
	HBoxContainer *path_hb = nullptr;
	HBoxContainer *status_bar = nullptr;
	RichTextLabel *current_scope_error_label = nullptr;
	Button *error_button = nullptr;
	ScrollContainer *error_scroll = nullptr;
	RichTextLabel *error_label = nullptr;

	AnimationTree *tree = nullptr;
	VBoxContainer *main_vbox_container = nullptr;
	bool resource_document = false;
	MarginContainer *editor_base = nullptr;

	Vector<String> button_path;
	Vector<String> edited_path;
	Vector<AnimationTreeNodeEditorPlugin *> editors;

	void _update_path();
	void _clear_editors();
	ObjectID current_root;

	void _path_button_pressed(int p_path);
	void _animation_list_changed();

	void _toggle_error_panel();
	void _update_error_message();

	static LocalVector<StringName> get_animation_list();

protected:
	void _meta_clicked(Variant p_meta);
	void _notification(int p_what);
	void _node_removed(Node *p_node);

	static AnimationTreeEditor *singleton;

public:
	AnimationTree *get_animation_tree() { return tree; }
	void add_plugin(AnimationTreeNodeEditorPlugin *p_editor);
	void remove_plugin(AnimationTreeNodeEditorPlugin *p_editor);

	String get_base_path();

	bool can_edit(const Ref<AnimationNode> &p_node) const;

	void edit_path(const Vector<String> &p_path);
	Vector<String> get_edited_path() const;

	void enter_editor(const String &p_path = "");
	static AnimationTreeEditor *get_singleton() { return singleton; }
	void edit(AnimationTree *p_tree);
	void set_document_host(Control *p_host);
	bool is_resource_document() const { return resource_document; }
	AnimationTreeEditor();
};

class AnimationTreeEditorPlugin : public EditorPlugin {
	GDCLASS(AnimationTreeEditorPlugin, EditorPlugin);

	AnimationTreeEditor *anim_tree_editor = nullptr;
	struct Document {
		Ref<AnimationRootNode> source;
		Ref<AnimationRootNode> root;
		Ref<AnimationRootNode> saved;
		Node3D *context = nullptr;
		AnimationPlayer *player = nullptr;
		AnimationTree *tree = nullptr;
		ObjectID scene_root;
		ObjectID scene_tree;
		ObjectID target_tree;
		int history = -99;
		bool writing = false;
		bool refresh_queued = false;
	};
	HashMap<String, Document> documents;
	String current_document;
	VBoxContainer *document_workspace = nullptr;
	VBoxContainer *graph_host = nullptr;
	Label *document_title = nullptr;
	EditorResourcePicker *skeleton_picker = nullptr;
	ConfirmationDialog *save_confirmation = nullptr;
	Callable pending_navigation;
	String pending_document;
	bool updating_picker = false;
	ObjectID action_tree;
	Label *storage_label = nullptr;
	Button *choose_shared_button = nullptr;
	Button *save_shared_button = nullptr;
	Button *embed_button = nullptr;
	Button *node_properties_button = nullptr;
	void _update_storage_actions();
	EditorFileDialog *shared_file = nullptr;
	Ref<AnimationGraphInspectorPlugin> inspector_plugin;
	void _choose_shared(ObjectID p_tree);
	void _open_shared_picker(ObjectID p_tree);
	void _open_shared_save_dialog(ObjectID p_tree);
	void _make_embedded(ObjectID p_tree);
	void _source_changed(const String &p_path);
	void _refresh_source(const String &p_path);
	void _shared_selected(const String &p_path, ObjectID p_tree);
	void _save_shared_as(ObjectID p_tree);
	void _shared_file_selected(const String &p_path);
	void _embed_graph(ObjectID p_tree);
	static Ref<SkeletonDefinition> _graph_skeleton(const Ref<AnimationRootNode> &p_graph);

	void _open_document(const Ref<AnimationRootNode> &p_root, bool p_activate = true, ObjectID p_target_tree = ObjectID());
	void _restore_document(const String &p_path);
	void _activate_document(const String &p_path);
	void _inspect_document(const String &p_path);
	void _inspect_node_properties();
	void _inspect_graph_properties();
	bool _save_document(const String &p_path);
	void _save_current_document();
	void _cancel_navigation();
	void _close_document(const String &p_path);
	void _clear_document_inspector(const Document &p_document);
	void _navigate_document(const Callable &p_action);
	void _save_and_navigate();
	void _discard_and_navigate(const String &p_action);
	void _continue_navigation();
	void _document_visibility_changed();
	void _return_to_scene();
	void _history_changed();
	void _register_resources(const Variant &p_value, int p_history, HashSet<ObjectID> &r_seen, bool p_root = false);
	void _skeleton_selected(const Ref<Resource> &p_resource);
	void _configure_skeleton(Document &p_document, const Ref<SkeletonDefinition> &p_skeleton);
	void _scene_closed(const String &p_path);
	void _scene_node_removed(Node *p_node);
	void _prune_scene_documents();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	virtual String get_plugin_name() const override { return "AnimationTree"; }
	virtual void edit(Object *p_object) override;
	virtual bool handles(Object *p_object) const override;
	virtual void make_visible(bool p_visible) override;
	virtual String get_unsaved_status(const String &p_for_scene = "") const override;
	virtual void save_external_data() override;
	void open_node_document(AnimationTree *p_tree, bool p_activate = true);
	Control *create_graph_actions(AnimationTree *p_tree);
	Error use_shared_graph(AnimationTree *p_tree, const String &p_path);
	Error save_shared_graph(AnimationTree *p_tree, const String &p_path);
	Error make_graph_embedded(AnimationTree *p_tree);

	AnimationTreeEditorPlugin();
};
