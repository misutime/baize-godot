/**************************************************************************/
/*  animation_tree_editor_plugin.cpp                                      */
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

#include "animation_tree_editor_plugin.h"

#include "core/io/dir_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/io/resource_uid.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/string/string_buffer.h"
#include "editor/animation/animation_blend_space_1d_editor.h"
#include "editor/animation/animation_blend_space_2d_editor.h"
#include "editor/animation/animation_blend_tree_editor_plugin.h"
#include "editor/animation/animation_state_machine_editor.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/docks/inspector_dock.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/gui/editor_bottom_panel.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/inspector/editor_resource_picker.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/scene/editor_scene_tabs.h"
#include "editor/settings/editor_command_palette.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/animation/animation_blend_tree.h"
#include "scene/animation/animation_player.h"
#include "scene/gui/button.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/texture_rect.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/skeleton_definition.h"

void AnimationTreeEditor::_meta_clicked(Variant p_meta) {
	if (p_meta.get_type() != Variant::STRING) {
		return;
	}
	const String raw_info_str = p_meta;
	const String full_path_str = raw_info_str.get_slice("::", 0);
	ERR_FAIL_COND(!full_path_str.ends_with("/"));

	int input_index = -1;
	if (raw_info_str.contains("::")) {
		const String input_index_str = raw_info_str.get_slice("::", 1);
		ERR_FAIL_COND(!input_index_str.is_valid_int());
		input_index = input_index_str.to_int();
	}

	AnimationTree *anim_tree = AnimationTreeEditor::get_singleton()->get_animation_tree();
	ERR_FAIL_NULL(anim_tree);

	// Check if the node at the full path can be entered directly.
	Ref<AnimationNode> target_node = anim_tree->get_animation_node_by_path(full_path_str);
	if (target_node.is_valid() && can_edit(target_node)) {
		const String to_edit_path = full_path_str.replace_first(Animation::PARAMETERS_BASE_PATH, "").trim_suffix("/");
		Vector<String> navigate_to;
		if (!to_edit_path.is_empty()) {
			navigate_to = to_edit_path.split("/");
		}
		if (AnimationTreeEditor::get_singleton()->get_edited_path() != navigate_to) {
			AnimationTreeEditor::get_singleton()->edit_path(navigate_to);
		}
		return;
	}

	// Otherwise, navigate to parent and pan to node.
	String parent_path = full_path_str.rstrip("/").substr(0, full_path_str.rstrip("/").rfind_char('/') + 1); // e.g. "parameters/blend_tree/node_name/" -> "parameters/blend_tree/"

	Ref<AnimationRootNode> root_node = anim_tree->get_animation_node_by_path(parent_path);
	ERR_FAIL_COND(root_node.is_null());

	const String to_edit_root_path = String(parent_path).replace_first(Animation::PARAMETERS_BASE_PATH, "").trim_suffix("/");
	Vector<String> navigate_to;
	if (!to_edit_root_path.is_empty()) {
		// empty string still has 1 element when split.
		navigate_to = to_edit_root_path.split("/");
	}
	if (AnimationTreeEditor::get_singleton()->get_edited_path() != navigate_to) {
		AnimationTreeEditor::get_singleton()->edit_path(navigate_to);
	}

	// Special case for AnimationNodeBlendTree.
	if (Ref<AnimationNodeBlendTree> blend_tree = root_node; blend_tree.is_valid()) {
		String child_name = full_path_str;
		child_name = child_name.replace_first(parent_path, "");
		child_name = child_name.trim_suffix("/");
		callable_mp(AnimationNodeBlendTreeEditor::get_singleton(), &AnimationNodeBlendTreeEditor::pan_to_node).call_deferred(child_name, input_index);
	}
}

void AnimationTreeEditor::_update_error_message() {
	if (resource_document) {
		// Resource documents validate without advancing animation playback.
		tree->clear_editor_validation_errors();
	}
	const StringName base_path = get_base_path();
	Ref<AnimationNode> inspected_node = tree->get_animation_node_by_path(base_path);
	if (inspected_node.is_valid()) {
		inspected_node->validate_node(tree, base_path);
		LocalVector<AnimationNode::ChildNode> children;
		inspected_node->get_child_nodes(&children);
		for (const AnimationNode::ChildNode &child : children) {
			if (child.node.is_valid()) {
				child.node->validate_node(tree, String(base_path) + String(child.name) + "/");
			}
		}
	}

	const String editor_error_message = tree->get_editor_error_message();
	const AHashMap<StringName, AnimationNode::InvalidInstance> &invalid_instances = tree->get_invalid_instances();

	static String last_error_key;
	if (editor_error_message.is_empty() && invalid_instances.is_empty()) {
		last_error_key = String();
		error_button->hide();
		error_scroll->hide();
		status_bar->hide(); // Since status bar is currently only used for errors.
		current_scope_error_label->clear();
		return;
	}

	// Cheaper to do this, than rebuild rich text label every frame.
	// Though it would be better, to only call this when the tree changes.
	{
		StringBuffer k;
		k += get_base_path();
		k += editor_error_message;
		for (const KeyValue<StringName, AnimationNode::InvalidInstance> &kv : invalid_instances) {
			k += kv.key;
			for (const String &reason : kv.value.errors) {
				k += reason;
			}
			for (const AnimationNode::InvalidInstance::InputError &E : kv.value.input_errors) {
				k += itos(E.index);
				k += E.error;
			}
		}

		String error_key = k.as_string();
		if (error_key == last_error_key) {
			return;
		}
		last_error_key = error_key;
	}

	// Get the currently open node by traversing edited_path.
	Ref<AnimationNode> current_node = tree->get_root_animation_node();
	for (const String &p : edited_path) {
		if (current_node.is_null()) {
			break;
		}
		current_node = current_node->get_child_by_name(p);
	}

	// Get direct children of current node for scope checking.
	LocalVector<AnimationNode::ChildNode> current_children;
	if (current_node.is_valid()) {
		current_node->get_child_nodes(&current_children);
	}

	error_label->clear();
	int count = 0;
	bool scope_error_found = false;
	StringName fallback_key;
	StringName downstream_key;

	if (!editor_error_message.is_empty()) {
		error_label->append_text(editor_error_message);
		error_label->add_newline();
		count++;
	}

	error_label->push_table(2);
	for (const KeyValue<StringName, AnimationNode::InvalidInstance> &kv : invalid_instances) {
		Ref<AnimationNode> node = tree->get_animation_node_by_path(kv.key);
		ERR_CONTINUE(node.is_null());
		count++;
		if (!scope_error_found && current_node.is_valid()) {
			if (fallback_key.is_empty()) {
				fallback_key = kv.key;
			}
			bool is_self = (node == current_node);
			bool is_leaf_child = false;
			bool is_editable_child = false;
			if (!is_self) {
				for (const AnimationNode::ChildNode &child : current_children) {
					if (child.node == node) {
						if (!can_edit(child.node)) {
							is_leaf_child = true;
						} else {
							is_editable_child = true;
						}
						break;
					}
				}
			}
			if (is_self || is_leaf_child) {
				scope_error_found = true;
				fallback_key = kv.key;
			} else if ((is_editable_child || String(kv.key).begins_with(get_base_path())) && downstream_key.is_empty()) {
				downstream_key = kv.key;
			}
		}

		error_label->push_cell();
		error_label->push_color(error_label->get_theme_color(SNAME("error_color"), EditorStringName(Editor)));
		error_label->append_text(vformat(RTR("%s at "), node->get_class()));
		error_label->push_meta(String(kv.key));
		error_label->append_text(vformat(RTR("'%s':"), kv.key));
		error_label->pop(); // Meta.
		error_label->pop(); // Color.
		error_label->pop(); // Cell.

		error_label->push_cell();
		error_label->push_color(error_label->get_theme_color(SceneStringName(font_color), EditorStringName(Editor)));
		for (const String &reason : kv.value.errors) {
			error_label->append_text(reason);
			error_label->add_newline();
		}

		const String input_error_base = String(kv.key) + "::";
		for (const AnimationNode::InvalidInstance::InputError &input_error : kv.value.input_errors) {
			const String input_name = node->get_input_name(input_error.index);
			error_label->append_text(input_error.error + " ");
			error_label->push_meta(input_error_base + itos(input_error.index));
			error_label->append_text(vformat(RTR("input %d '%s'."), input_error.index, input_name));
			error_label->pop(); // Meta.
			error_label->append_text(" ");
		}
		error_label->pop(); // Color.
		error_label->pop(); // Cell.
	}
	error_label->pop(); // Table.

	current_scope_error_label->clear();
	{
		const StringName display_key = scope_error_found ? fallback_key : (!downstream_key.is_empty() ? downstream_key : fallback_key);
		if (!display_key.is_empty()) {
			const AnimationNode::InvalidInstance &inst = invalid_instances.get(display_key);
			Ref<AnimationNode> node = tree->get_animation_node_by_path(display_key);
			const String input_error_base = String(display_key) + "::";
			current_scope_error_label->push_color(current_scope_error_label->get_theme_color(SNAME("error_color"), EditorStringName(Editor)));
			current_scope_error_label->append_text(TTR("Error: "));
			if (!inst.errors.is_empty()) {
				current_scope_error_label->push_meta(String(display_key));
				current_scope_error_label->append_text(inst.errors[0]);
				current_scope_error_label->pop(); // Meta.
			} else if (!inst.input_errors.is_empty() && node.is_valid()) {
				current_scope_error_label->push_meta(input_error_base + itos(inst.input_errors[0].index));
				current_scope_error_label->append_text(inst.input_errors[0].error + " ");
				current_scope_error_label->append_text(vformat(RTR("input %d '%s'."), inst.input_errors[0].index, node->get_input_name(inst.input_errors[0].index)));
				current_scope_error_label->pop(); // Meta.
			}
			current_scope_error_label->pop(); // Color.
		}
	}

	error_button->set_text(itos(count));
	error_button->show();
	status_bar->show();
}

void AnimationTreeEditor::edit(AnimationTree *p_tree) {
	if (p_tree && !p_tree->is_connected("animation_list_changed", callable_mp(this, &AnimationTreeEditor::_animation_list_changed))) {
		p_tree->connect("animation_list_changed", callable_mp(this, &AnimationTreeEditor::_animation_list_changed), CONNECT_DEFERRED);
	}

	if (tree == p_tree) {
		return;
	}

	if (tree && tree->is_connected("animation_list_changed", callable_mp(this, &AnimationTreeEditor::_animation_list_changed))) {
		tree->disconnect("animation_list_changed", callable_mp(this, &AnimationTreeEditor::_animation_list_changed));
	}

	tree = p_tree;

	Vector<String> path;
	if (tree) {
		edit_path(path);
	} else {
		_clear_editors();
	}
}

void AnimationTreeEditor::_node_removed(Node *p_node) {
	if (p_node == tree) {
		tree = nullptr;
		_clear_editors();
	}
}

void AnimationTreeEditor::_path_button_pressed(int p_path) {
	edited_path.clear();
	for (int i = 0; i <= p_path; i++) {
		edited_path.push_back(button_path[i]);
	}
}

void AnimationTreeEditor::_animation_list_changed() {
	AnimationNodeBlendTreeEditor *bte = AnimationNodeBlendTreeEditor::get_singleton();
	if (bte) {
		bte->update_graph();
	}
}

void AnimationTreeEditor::_update_path() {
	while (path_hb->get_child_count() > 1) {
		memdelete(path_hb->get_child(1));
	}

	Ref<ButtonGroup> group;
	group.instantiate();

	Button *b = memnew(Button);
	b->set_text(TTR("Root"));
	b->set_toggle_mode(true);
	b->set_button_group(group);
	b->set_pressed(true);
	b->set_focus_mode(FOCUS_ACCESSIBILITY);
	b->connect(SceneStringName(pressed), callable_mp(this, &AnimationTreeEditor::_path_button_pressed).bind(-1));
	path_hb->add_child(b);
	for (int i = 0; i < button_path.size(); i++) {
		// bread crumbs.
		TextureRect *texture_rect = memnew(TextureRect);
		texture_rect->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		texture_rect->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		texture_rect->set_custom_minimum_size(Size2(16, 16) * EDSCALE);
		texture_rect->set_texture(get_editor_theme_icon(SNAME("GuiTreeArrowRight")));
		path_hb->add_child(texture_rect);

		b = memnew(Button);
		b->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
		b->set_text(button_path[i]);
		b->set_toggle_mode(true);
		b->set_button_group(group);
		path_hb->add_child(b);
		b->set_pressed(true);
		b->set_focus_mode(FOCUS_ACCESSIBILITY);
		b->connect(SceneStringName(pressed), callable_mp(this, &AnimationTreeEditor::_path_button_pressed).bind(i));
	}
}

void AnimationTreeEditor::edit_path(const Vector<String> &p_path) {
	button_path.clear();

	Ref<AnimationNode> node = tree->get_root_animation_node();

	if (node.is_valid()) {
		current_root = node->get_instance_id();

		for (int i = 0; i < p_path.size(); i++) {
			Ref<AnimationNode> child = node->get_child_by_name(p_path[i]);
			ERR_BREAK_MSG(child.is_null(), vformat("Cannot edit path '%s': node '%s' not found as child of '%s'.", String("/").join(p_path), p_path[i], node->get_class()));
			if (resource_document && !child->is_built_in() && child->get_path().has_extension("animtree")) {
				EditorInterface::get_singleton()->edit_resource(child);
				return;
			}
			node = child;
			button_path.push_back(p_path[i]);
		}

		edited_path = button_path;

		for (int i = 0; i < editors.size(); i++) {
			if (editors[i]->can_edit(node)) {
				editors[i]->edit(node);
				editors[i]->show();
			} else {
				editors[i]->edit(Ref<AnimationNode>());
				editors[i]->hide();
			}
		}
	} else {
		current_root = ObjectID();
		edited_path = button_path;
		for (int i = 0; i < editors.size(); i++) {
			editors[i]->edit(Ref<AnimationNode>());
			editors[i]->hide();
		}
	}

	_update_path();
	_update_error_message();
}

void AnimationTreeEditor::_clear_editors() {
	button_path.clear();
	current_root = ObjectID();
	edited_path = button_path;
	for (int i = 0; i < editors.size(); i++) {
		editors[i]->edit(Ref<AnimationNode>());
		editors[i]->hide();
	}
	_update_path();
}

Vector<String> AnimationTreeEditor::get_edited_path() const {
	return button_path;
}

void AnimationTreeEditor::enter_editor(const String &p_path) {
	Vector<String> path = edited_path;
	path.push_back(p_path);
	edit_path(path);
}

void AnimationTreeEditor::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			Ref<StyleBoxEmpty> empty_style;
			empty_style.instantiate();
			error_scroll->add_theme_style_override(SceneStringName(panel), empty_style);
			error_label->add_theme_color_override(SNAME("default_color"), get_theme_color(SNAME("error_color"), EditorStringName(Editor)));
			error_button->set_button_icon(get_editor_theme_icon(SNAME("StatusError")));
			error_button->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("error_color"), EditorStringName(Editor)));
			current_scope_error_label->add_theme_font_override(SNAME("normal_font"), get_theme_font(SNAME("main"), EditorStringName(EditorFonts)));
			current_scope_error_label->add_theme_font_size_override(SNAME("normal_font_size"), get_theme_font_size(SNAME("main_size"), EditorStringName(EditorFonts)));
			current_scope_error_label->add_theme_color_override(SNAME("default_color"), get_theme_color(SNAME("error_color"), EditorStringName(Editor)));
			current_scope_error_label->add_theme_style_override(SNAME("normal"), get_theme_stylebox(SNAME("normal"), SNAME("Label")));
		} break;

		case NOTIFICATION_PROCESS: {
			ObjectID root;
			if (tree && tree->get_root_animation_node().is_valid()) {
				root = tree->get_root_animation_node()->get_instance_id();
			}

			if (root != current_root) {
				edit_path(Vector<String>());
			}

			if (button_path.size() != edited_path.size()) {
				edit_path(edited_path);
			}

			if (tree) {
				_update_error_message();
			}
		} break;

		case NOTIFICATION_ENTER_TREE: {
			get_tree()->connect("node_removed", callable_mp(this, &AnimationTreeEditor::_node_removed));
		} break;

		case NOTIFICATION_EXIT_TREE: {
			get_tree()->disconnect("node_removed", callable_mp(this, &AnimationTreeEditor::_node_removed));
		} break;
	}
}

AnimationTreeEditor *AnimationTreeEditor::singleton = nullptr;

void AnimationTreeEditor::add_plugin(AnimationTreeNodeEditorPlugin *p_editor) {
	ERR_FAIL_COND(p_editor->get_parent());
	editor_base->add_child(p_editor);
	editors.push_back(p_editor);
	p_editor->set_h_size_flags(SIZE_EXPAND_FILL);
	p_editor->set_v_size_flags(SIZE_EXPAND_FILL);
	p_editor->hide();
}

void AnimationTreeEditor::remove_plugin(AnimationTreeNodeEditorPlugin *p_editor) {
	ERR_FAIL_COND(p_editor->get_parent() != editor_base);
	editor_base->remove_child(p_editor);
	editors.erase(p_editor);
}

String AnimationTreeEditor::get_base_path() {
	String path = Animation::PARAMETERS_BASE_PATH;
	for (int i = 0; i < edited_path.size(); i++) {
		path += edited_path[i] + "/";
	}
	return path;
}

bool AnimationTreeEditor::can_edit(const Ref<AnimationNode> &p_node) const {
	for (int i = 0; i < editors.size(); i++) {
		if (editors[i]->can_edit(p_node)) {
			return true;
		}
	}
	return false;
}

LocalVector<StringName> AnimationTreeEditor::get_animation_list() {
	// This can be called off the main thread due to resource preview generation. Quit early in that case.
	if (!singleton->tree || !Thread::is_main_thread() || !singleton->main_vbox_container->is_visible_in_tree()) {
		// When tree is empty, singleton not in the main thread.
		return LocalVector<StringName>();
	}

	AnimationTree *tree = singleton->tree;
	if (!tree) {
		return LocalVector<StringName>();
	}

	return tree->get_sorted_animation_list();
}

void AnimationTreeEditor::_toggle_error_panel() {
	error_scroll->set_visible(!error_scroll->is_visible());
}

AnimationTreeEditor::AnimationTreeEditor() {
	singleton = this;
	AnimationNodeAnimation::get_editable_animation_list = get_animation_list;

	set_name(TTRC("AnimationTree"));
	set_icon_name("AnimationTreeDock");
	set_dock_shortcut(ED_SHORTCUT_AND_COMMAND("bottom_panels/toggle_animation_tree_bottom_panel", TTRC("Toggle AnimationTree Dock")));
	set_default_slot(EditorDock::DOCK_SLOT_BOTTOM);
	set_available_layouts(EditorDock::DOCK_LAYOUT_HORIZONTAL | EditorDock::DOCK_LAYOUT_FLOATING);
	set_global(false);
	set_transient(true);

	main_vbox_container = memnew(VBoxContainer);
	add_child(main_vbox_container);

	path_edit = memnew(ScrollContainer);
	path_edit->set_vertical_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	main_vbox_container->add_child(path_edit);

	path_hb = memnew(HBoxContainer);
	path_hb->add_child(memnew(Label(TTR("Path:"))));
	path_edit->add_child(path_hb);

	main_vbox_container->add_child(memnew(HSeparator));

	VSplitContainer *split = memnew(VSplitContainer);
	split->set_v_size_flags(SIZE_EXPAND_FILL);
	main_vbox_container->add_child(split);

	VBoxContainer *editor_vbox = memnew(VBoxContainer);
	editor_vbox->set_v_size_flags(SIZE_EXPAND_FILL);
	split->add_child(editor_vbox);

	editor_base = memnew(MarginContainer);
	editor_base->set_v_size_flags(SIZE_EXPAND_FILL);
	editor_vbox->add_child(editor_base);

	status_bar = memnew(HBoxContainer);
	editor_vbox->add_child(status_bar);
	status_bar->hide();

	current_scope_error_label = memnew(RichTextLabel);
	current_scope_error_label->set_fit_content(true);
	current_scope_error_label->set_h_size_flags(SIZE_EXPAND_FILL);
	current_scope_error_label->set_v_size_flags(SIZE_SHRINK_CENTER);
	current_scope_error_label->set_meta_underline(false);
	current_scope_error_label->connect("meta_clicked", callable_mp(this, &AnimationTreeEditor::_meta_clicked));
	status_bar->add_child(current_scope_error_label);

	error_button = memnew(Button);
	error_button->set_flat(true);
	error_button->set_h_size_flags(SIZE_SHRINK_END);
	error_button->set_v_size_flags(SIZE_EXPAND | SIZE_SHRINK_CENTER);
	error_button->set_default_cursor_shape(CURSOR_POINTING_HAND);
	error_button->hide();
	error_button->connect(SceneStringName(pressed), callable_mp(this, &AnimationTreeEditor::_toggle_error_panel));
	error_button->set_tooltip_text(TTRC("Errors"));
	status_bar->add_child(error_button);

	add_plugin(memnew(AnimationNodeBlendTreeEditor));
	add_plugin(memnew(AnimationNodeBlendSpace1DEditor));
	add_plugin(memnew(AnimationNodeBlendSpace2DEditor));
	add_plugin(memnew(AnimationNodeStateMachineEditor));

	error_scroll = memnew(ScrollContainer);
	error_scroll->set_h_size_flags(SIZE_EXPAND_FILL);
	error_scroll->set_v_size_flags(SIZE_EXPAND_FILL);
	error_scroll->set_custom_minimum_size(Size2(0, 100) * EDSCALE);
	error_scroll->set_vertical_scroll_mode(ScrollContainer::SCROLL_MODE_AUTO);
	split->add_child(error_scroll);

	error_label = memnew(RichTextLabel);
	error_label->set_selection_enabled(true);
	error_label->set_focus_mode(FOCUS_ACCESSIBILITY);
	error_label->set_h_size_flags(SIZE_EXPAND_FILL);
	error_label->set_v_size_flags(SIZE_EXPAND_FILL);
	error_label->set_meta_underline(true);
	error_label->connect("meta_clicked", callable_mp(this, &AnimationTreeEditor::_meta_clicked));
	error_scroll->add_child(error_label);
	error_scroll->hide();
}

void AnimationTreeEditor::set_document_host(Control *p_host) {
	resource_document = p_host != nullptr;
	Node *parent = p_host ? static_cast<Node *>(p_host) : this;
	if (main_vbox_container->get_parent() != parent) {
		main_vbox_container->reparent(parent);
	}
	main_vbox_container->set_v_size_flags(SIZE_EXPAND_FILL);
}

void AnimationTreeEditorPlugin::_register_resources(const Variant &p_value, int p_history, HashSet<ObjectID> &r_seen, bool p_root) {
	if (p_value.get_type() == Variant::OBJECT) {
		Ref<Resource> resource = p_value;
		if (resource.is_null() || r_seen.has(resource->get_instance_id()) || (!p_root && !resource->is_built_in())) {
			return;
		}
		r_seen.insert(resource->get_instance_id());
		EditorUndoRedoManager::get_singleton()->register_document_object(resource.ptr(), p_history);
		List<PropertyInfo> properties;
		resource->get_property_list(&properties);
		for (const PropertyInfo &property : properties) {
			if (property.usage & PROPERTY_USAGE_STORAGE) {
				_register_resources(resource->get(property.name), p_history, r_seen);
			}
		}
	} else if (p_value.get_type() == Variant::ARRAY) {
		const Array values = p_value;
		for (const Variant &value : values) {
			_register_resources(value, p_history, r_seen);
		}
	} else if (p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary values = p_value;
		for (const Variant &key : values.get_key_list()) {
			_register_resources(values[key], p_history, r_seen);
		}
	}
}

void AnimationTreeEditorPlugin::_history_changed() {
	EditorUndoRedoManager *undo = EditorUndoRedoManager::get_singleton();
	for (KeyValue<String, Document> &entry : documents) {
		if (entry.value.scene_tree.is_valid() && !undo->has_history(entry.value.history)) {
			continue;
		}
		HashSet<ObjectID> seen;
		_register_resources(entry.value.root, entry.value.history, seen, true);
		EditorSceneTabs::get_singleton()->set_document_modified(entry.key, undo->is_history_unsaved(entry.value.history));
	}
}

void AnimationTreeEditorPlugin::_open_document(const Ref<AnimationRootNode> &p_root, bool p_activate, ObjectID p_target_tree) {
	const String path = p_root->get_path();
	if (!documents.has(path)) {
		Document document;
		document.source = p_root;
		document.root = p_root->duplicate(true);
		document.saved = document.root->duplicate(true);
		document.history = EditorUndoRedoManager::get_singleton()->create_document_history();
		document.context = memnew(Node3D);
		document.context->set_name("AnimationTreeDocumentContext");
		document_workspace->add_child(document.context);
		document.player = memnew(AnimationPlayer);
		document.player->set_name("AnimationPlayer");
		document.context->add_child(document.player);
		document.tree = memnew(AnimationTree);
		document.tree->set_name("AnimationTree");
		document.context->add_child(document.tree);
		document.tree->set_callback_mode_process(AnimationMixer::ANIMATION_CALLBACK_MODE_PROCESS_MANUAL);
		document.tree->set_animation_player(NodePath("../AnimationPlayer"));
		document.tree->set_root_animation_node(document.root);
		EditorUndoRedoManager::get_singleton()->register_document_object(document.tree, document.history);
		documents.insert(path, document);
		p_root->connect_changed(callable_mp(this, &AnimationTreeEditorPlugin::_source_changed).bind(path));
		_configure_skeleton(documents[path], _graph_skeleton(p_root));
	}
	documents[path].target_tree = p_target_tree;
	EditorSceneTabs::get_singleton()->open_document(path, path.get_file(), anim_tree_editor->get_theme_icon(SNAME("AnimationTree"), EditorStringName(EditorIcons)), callable_mp(this, &AnimationTreeEditorPlugin::_activate_document).bind(path), callable_mp(this, &AnimationTreeEditorPlugin::_save_document).bind(path), callable_mp(this, &AnimationTreeEditorPlugin::_close_document).bind(path), callable_mp(this, &AnimationTreeEditorPlugin::_navigate_document), Callable(), p_activate);
	_history_changed();
}

void AnimationTreeEditorPlugin::open_node_document(AnimationTree *p_tree, bool p_activate) {
	ERR_FAIL_NULL(p_tree);
	AnimationPlayer *player = Object::cast_to<AnimationPlayer>(p_tree->get_node_or_null(p_tree->get_animation_player()));
	Ref<AnimationRootNode> graph = p_tree->get_root_animation_node();
	if (graph.is_valid() && !graph->is_built_in()) {
		Vector<String> embedded_documents;
		for (const KeyValue<String, Document> &entry : documents) {
			if (entry.value.scene_tree == p_tree->get_instance_id()) {
				embedded_documents.push_back(entry.key);
			}
		}
		for (const String &path : embedded_documents) {
			EditorSceneTabs::get_singleton()->remove_document(path);
			_close_document(path);
		}
		_open_document(graph, false, p_tree->get_instance_id());
		if (player) {
			Document &document = documents[graph->get_path()];
			_configure_skeleton(document, player->get_skeleton_definition());
			LocalVector<StringName> libraries;
			player->get_animation_library_list(&libraries);
			for (const StringName &name : libraries) {
				if (document.player->has_animation_library(name)) {
					document.player->remove_animation_library(name);
				}
				document.player->add_animation_library(name, player->get_animation_library(name));
			}
		}
		if (p_activate) {
			_open_document(graph, true, p_tree->get_instance_id());
		}
		return;
	}
	EditorData &scene_data = EditorNode::get_editor_data();
	for (int scene = 0; scene < scene_data.get_edited_scene_count(); scene++) {
		Node *root = scene_data.get_edited_scene_root(scene);
		if (!root || (root != p_tree && !root->is_ancestor_of(p_tree))) {
			continue;
		}
		const int history = scene_data.get_scene_history_id(scene);
		if (graph.is_null()) {
			Ref<AnimationNodeBlendTree> blend_tree;
			blend_tree.instantiate();
			EditorUndoRedoManager *undo = EditorUndoRedoManager::get_singleton();
			undo->create_action_for_history(TTR("Create Animation Tree"), history);
			undo->add_do_property(p_tree, "tree_root", blend_tree);
			undo->add_undo_property(p_tree, "tree_root", Variant());
			undo->commit_action();
			graph = blend_tree;
		}
		const String scene_path = root->get_scene_file_path().is_empty() ? "unsaved://" + itos(root->get_instance_id()) : root->get_scene_file_path();
		const String path = scene_path + "::AnimationTree/" + String(root->get_path_to(p_tree));
		if (!documents.has(path)) {
			Document document;
			document.source = graph;
			document.root = graph;
			document.tree = p_tree;
			document.player = player;
			document.scene_root = root->get_instance_id();
			document.scene_tree = p_tree->get_instance_id();
			document.history = history;
			documents.insert(path, document);
		}
		EditorSceneTabs::get_singleton()->open_document(path, (root->get_scene_file_path().is_empty() ? String(root->get_name()) : root->get_scene_file_path().get_file().get_basename()) + " / " + String(p_tree->get_name()), anim_tree_editor->get_theme_icon(SNAME("AnimationTree"), EditorStringName(EditorIcons)), callable_mp(this, &AnimationTreeEditorPlugin::_activate_document).bind(path), callable_mp(this, &AnimationTreeEditorPlugin::_save_document).bind(path), callable_mp(this, &AnimationTreeEditorPlugin::_close_document).bind(path), Callable(), Callable(), p_activate);
		_history_changed();
		return;
	}
}

void AnimationTreeEditorPlugin::_restore_document(const String &p_path) {
	if (p_path.contains("::AnimationTree/")) {
		for (const Variant &value : EditorInterface::get_singleton()->get_open_scene_roots()) {
			Node *root = Object::cast_to<Node>(value);
			if (root->get_scene_file_path() == p_path.get_slice("::", 0)) {
				AnimationTree *tree = Object::cast_to<AnimationTree>(root->get_node_or_null(NodePath(p_path.get_slice("::AnimationTree/", 1))));
				if (tree) {
					open_node_document(tree, false);
				}
				return;
			}
		}
		return;
	}
	if (p_path.contains("::") || (!p_path.has_extension("animtree") && !ClassDB::is_parent_class(ResourceLoader::get_resource_type(p_path), AnimationRootNode::get_class_static()))) {
		return;
	}
	Ref<AnimationRootNode> root = ResourceLoader::load(p_path);
	if (root.is_valid()) {
		_open_document(root, false);
	}
}

void AnimationTreeEditorPlugin::_activate_document(const String &p_path) {
	Document &document = documents[p_path];
	if (document.scene_tree.is_valid()) {
		document.tree = Object::cast_to<AnimationTree>(ObjectDB::get_instance(document.scene_tree));
		if (!document.tree) {
			EditorSceneTabs::get_singleton()->remove_document(p_path);
			_close_document(p_path);
			return;
		}
		document.root = document.tree->get_root_animation_node();
		document.player = Object::cast_to<AnimationPlayer>(document.tree->get_node_or_null(document.tree->get_animation_player()));
	}
	current_document = p_path;
	action_tree = document.scene_tree.is_valid() ? document.scene_tree : document.target_tree;
	storage_label->set_text(document.scene_tree.is_valid() ? TTR("Embedded in Scene") : TTR("Shared Animation Graph"));
	_update_storage_actions();
	document_title->set_text(document.scene_tree.is_valid() ? p_path.get_slice("::", 0).get_file() + " / " + String(document.tree->get_name()) : p_path.get_file());
	document_title->set_tooltip_text(p_path);
	anim_tree_editor->hide();
	Node3DEditor::get_singleton()->show_document_workspace(document_workspace);
	anim_tree_editor->set_document_host(graph_host);
	anim_tree_editor->edit(document.tree);
	anim_tree_editor->set_process(true);
	updating_picker = true;
	skeleton_picker->set_editable(document.scene_tree.is_null());
	skeleton_picker->set_edited_resource(document.player ? document.player->get_skeleton_definition() : Ref<SkeletonDefinition>());
	updating_picker = false;
	EditorUndoRedoManager::get_singleton()->set_active_document_history(document.history);
	EditorDockManager::get_singleton()->force_focus_dock(Node3DEditor::get_singleton());
	callable_mp(this, &AnimationTreeEditorPlugin::_inspect_document).call_deferred(p_path);
}

void AnimationTreeEditorPlugin::_inspect_document(const String &p_path) {
	if (EditorSceneTabs::get_singleton()->get_active_document() == p_path && documents.has(p_path)) {
		const Document &document = documents[p_path];
		const ObjectID target_id = document.scene_tree.is_valid() ? document.scene_tree : document.target_tree;
		AnimationTree *target = Object::cast_to<AnimationTree>(ObjectDB::get_instance(target_id));
		EditorNode::get_singleton()->push_item(target ? static_cast<Object *>(target) : document.root.ptr(), "", true);
	}
}

void AnimationTreeEditorPlugin::_inspect_node_properties() {
	_inspect_document(current_document);
}

void AnimationTreeEditorPlugin::_inspect_graph_properties() {
	if (documents.has(current_document)) {
		EditorNode::get_singleton()->push_item(documents[current_document].root.ptr(), "", true);
	}
}

bool AnimationTreeEditorPlugin::_save_document(const String &p_path) {
	Document &document = documents[p_path];
	if (document.scene_tree.is_valid()) {
		Node *root = Object::cast_to<Node>(ObjectDB::get_instance(document.scene_root));
		const Error error = EditorInterface::get_singleton()->save_scene_root(root);
		if (error != OK) {
			EditorNode::get_singleton()->show_warning(TTR("Save the owning scene before saving its animation tree."));
			return false;
		}
		_history_changed();
		return true;
	}
	const Error error = ResourceSaver::save(document.root, p_path);
	if (error != OK) {
		EditorNode::get_singleton()->show_warning(vformat(TTR("Cannot save '%s': %s"), p_path, error_names[error]));
		return false;
	}
	document.writing = true;
	document.source->copy_from(document.root->duplicate(true));
	document.source->emit_changed();
	document.writing = false;
	document.saved = document.root->duplicate(true);
	EditorUndoRedoManager::get_singleton()->set_history_as_saved(document.history);
	_history_changed();
	return true;
}

void AnimationTreeEditorPlugin::_close_document(const String &p_path) {
	Document document = documents[p_path];
	_clear_document_inspector(document);
	documents.erase(p_path);
	if (current_document == p_path) {
		if (document_workspace->is_visible_in_tree()) {
			Node3DEditor::get_singleton()->show_document_workspace(nullptr);
		}
		anim_tree_editor->edit(nullptr);
		anim_tree_editor->set_document_host(nullptr);
		current_document = String();
	}
	if (document.scene_tree.is_null()) {
		document.source->disconnect_changed(callable_mp(this, &AnimationTreeEditorPlugin::_source_changed).bind(p_path));
		EditorUndoRedoManager::get_singleton()->remove_document_history(document.history);
		memdelete(document.context);
	}
}

void AnimationTreeEditorPlugin::_clear_document_inspector(const Document &p_document) {
	if (p_document.scene_tree.is_valid()) {
		return;
	}
	EditorInspector *inspector = InspectorDock::get_inspector_singleton();
	Object *inspected = inspector->get_edited_object();
	if (inspected && EditorUndoRedoManager::get_singleton()->get_history_id_for_object(inspected) == p_document.history) {
		// Property controls hold raw pointers and must detach before the working copy is released.
		inspector->edit(nullptr);
		InspectorDock::get_singleton()->update(nullptr);
	}
}

void AnimationTreeEditorPlugin::_source_changed(const String &p_path) {
	Document *document = documents.getptr(p_path);
	if (!document || document->writing || document->refresh_queued || EditorUndoRedoManager::get_singleton()->is_history_unsaved(document->history)) {
		return;
	}
	document->refresh_queued = true;
	callable_mp(this, &AnimationTreeEditorPlugin::_refresh_source).call_deferred(p_path);
}

void AnimationTreeEditorPlugin::_refresh_source(const String &p_path) {
	if (!documents.has(p_path)) {
		return;
	}
	Document &document = documents[p_path];
	document.refresh_queued = false;
	EditorUndoRedoManager *undo = EditorUndoRedoManager::get_singleton();
	if (undo->is_history_unsaved(document.history)) {
		return;
	}
	_clear_document_inspector(document);
	undo->remove_document_history(document.history);
	document.root = document.source->duplicate(true);
	document.saved = document.root->duplicate(true);
	document.history = undo->create_document_history();
	undo->register_document_object(document.tree, document.history);
	document.tree->set_root_animation_node(document.root);
	_configure_skeleton(document, _graph_skeleton(document.root));
	if (current_document == p_path && document_workspace->is_visible_in_tree()) {
		anim_tree_editor->edit(nullptr);
		anim_tree_editor->edit(document.tree);
		undo->set_active_document_history(document.history);
		_inspect_document(p_path);
	}
	_history_changed();
}

void AnimationTreeEditorPlugin::_scene_closed(const String &p_path) {
	Vector<String> paths;
	for (const KeyValue<String, Document> &entry : documents) {
		Node *root = Object::cast_to<Node>(ObjectDB::get_instance(entry.value.scene_root));
		if (root && root->get_scene_file_path() == p_path) {
			paths.push_back(entry.key);
		}
	}
	for (const String &path : paths) {
		EditorSceneTabs::get_singleton()->remove_document(path);
		_close_document(path);
	}
}

void AnimationTreeEditorPlugin::_scene_node_removed(Node *p_node) {
	for (const KeyValue<String, Document> &entry : documents) {
		if (entry.value.scene_tree == p_node->get_instance_id()) {
			callable_mp(this, &AnimationTreeEditorPlugin::_prune_scene_documents).call_deferred();
			return;
		}
	}
}

void AnimationTreeEditorPlugin::_prune_scene_documents() {
	Vector<String> paths;
	for (const KeyValue<String, Document> &entry : documents) {
		if (entry.value.scene_tree.is_null()) {
			continue;
		}
		Node *tree = Object::cast_to<Node>(ObjectDB::get_instance(entry.value.scene_tree));
		Node *root = Object::cast_to<Node>(ObjectDB::get_instance(entry.value.scene_root));
		if (!tree || !root || (tree != root && !root->is_ancestor_of(tree))) {
			paths.push_back(entry.key);
		}
	}
	for (const String &path : paths) {
		EditorSceneTabs::get_singleton()->remove_document(path);
		_close_document(path);
	}
}

void AnimationTreeEditorPlugin::_navigate_document(const Callable &p_action) {
	if (documents.has(current_document) && documents[current_document].scene_tree.is_null() && EditorUndoRedoManager::get_singleton()->is_history_unsaved(documents[current_document].history)) {
		pending_document = current_document;
		pending_navigation = p_action;
		save_confirmation->popup_centered();
	} else {
		p_action.call();
	}
}

void AnimationTreeEditorPlugin::_save_and_navigate() {
	if (_save_document(pending_document)) {
		_continue_navigation();
	}
}

void AnimationTreeEditorPlugin::_discard_and_navigate(const String &p_action) {
	save_confirmation->hide();
	Document &document = documents[pending_document];
	EditorUndoRedoManager *undo = EditorUndoRedoManager::get_singleton();
	_clear_document_inspector(document);
	undo->remove_document_history(document.history);
	document.root = document.saved->duplicate(true);
	document.history = undo->create_document_history();
	undo->register_document_object(document.tree, document.history);
	document.tree->set_root_animation_node(document.root);
	anim_tree_editor->edit(nullptr);
	anim_tree_editor->edit(document.tree);
	undo->set_active_document_history(document.history);
	_history_changed();
	_continue_navigation();
}

void AnimationTreeEditorPlugin::_continue_navigation() {
	const Callable action = pending_navigation;
	pending_navigation = Callable();
	pending_document = String();
	if (action.is_valid()) {
		action.call();
	}
}

void AnimationTreeEditorPlugin::_document_visibility_changed() {
	if (document_workspace->is_visible_in_tree() && documents.has(current_document) && EditorSceneTabs::get_singleton()->get_active_document() == current_document) {
		anim_tree_editor->set_process(true);
		EditorUndoRedoManager::get_singleton()->set_active_document_history(documents[current_document].history);
	} else {
		anim_tree_editor->set_process(false);
		EditorUndoRedoManager::get_singleton()->set_active_document_history(EditorUndoRedoManager::INVALID_HISTORY);
	}
}

void AnimationTreeEditorPlugin::_return_to_scene() {
	if (document_workspace->is_visible_in_tree()) {
		Node3DEditor::get_singleton()->show_document_workspace(nullptr);
	}
	if (!current_document.is_empty()) {
		anim_tree_editor->edit(nullptr);
		anim_tree_editor->set_document_host(nullptr);
		current_document = String();
	}
}

void AnimationTreeEditorPlugin::_configure_skeleton(Document &p_document, const Ref<SkeletonDefinition> &p_skeleton) {
	Node *previous = p_document.context->get_node_or_null(NodePath("Skeleton3D"));
	if (previous) {
		memdelete(previous);
	}
	p_document.player->set_skeleton_definition(p_skeleton);
	if (p_skeleton.is_valid()) {
		Skeleton3D *skeleton = memnew(Skeleton3D);
		skeleton->set_name("Skeleton3D");
		for (int bone = 0; bone < p_skeleton->get_bone_count(); bone++) {
			skeleton->add_bone(p_skeleton->get_bone_name(bone));
			skeleton->set_bone_parent(bone, p_skeleton->get_bone_parent(bone));
			skeleton->set_bone_rest(bone, p_skeleton->get_bone_rest(bone));
		}
		p_document.context->add_child(skeleton);
	}
	p_document.tree->set_animation_player(NodePath("../AnimationPlayer"));
}

void AnimationTreeEditorPlugin::_skeleton_selected(const Ref<Resource> &p_resource) {
	if (updating_picker || !documents.has(current_document) || documents[current_document].scene_tree.is_valid()) {
		return;
	}
	Ref<SkeletonDefinition> skeleton = p_resource;
	_configure_skeleton(documents[current_document], skeleton);
	Document &document = documents[current_document];
	EditorUndoRedoManager *undo = EditorUndoRedoManager::get_singleton();
	undo->create_action_for_history(TTR("Set Animation Graph Skeleton"), document.history);
	undo->add_do_method(document.root.ptr(), "set_meta", "_skeleton_path", skeleton.is_valid() ? skeleton->get_path() : String());
	undo->add_do_method(document.root.ptr(), "set_meta", "_skeleton_uid", skeleton.is_valid() ? ResourceLoader::get_resource_uid(skeleton->get_path()) : ResourceUID::INVALID_ID);
	undo->add_undo_method(document.root.ptr(), "set_meta", "_skeleton_path", document.root->get_meta("_skeleton_path", Variant()));
	undo->add_undo_method(document.root.ptr(), "set_meta", "_skeleton_uid", document.root->get_meta("_skeleton_uid", Variant()));
	undo->commit_action();
	anim_tree_editor->edit(nullptr);
	anim_tree_editor->edit(documents[current_document].tree);
}

void AnimationTreeEditorPlugin::edit(Object *p_object) {
	Ref<AnimationRootNode> root = Object::cast_to<AnimationRootNode>(p_object);
	if (root.is_valid() && !root->is_built_in()) {
		_open_document(root);
		return;
	}
	if (current_document.is_empty()) {
		anim_tree_editor->edit(nullptr);
	}
}

bool AnimationTreeEditorPlugin::handles(Object *p_object) const {
	const Ref<AnimationRootNode> root = Object::cast_to<AnimationRootNode>(p_object);
	return root.is_valid() && !root->is_built_in();
}

void AnimationTreeEditorPlugin::make_visible(bool p_visible) {
	if (document_workspace->is_visible_in_tree()) {
		anim_tree_editor->set_process(true);
		return;
	}
	anim_tree_editor->hide();
	anim_tree_editor->set_process(false);
}

String AnimationTreeEditorPlugin::get_unsaved_status(const String &p_for_scene) const {
	if (p_for_scene.is_empty()) {
		for (const KeyValue<String, Document> &entry : documents) {
			if (entry.value.scene_tree.is_null() && EditorUndoRedoManager::get_singleton()->is_history_unsaved(entry.value.history)) {
				return TTR("An animation tree has unsaved changes.");
			}
		}
	}
	return String();
}

void AnimationTreeEditorPlugin::save_external_data() {
	for (const KeyValue<String, Document> &entry : documents) {
		if (entry.value.scene_tree.is_null() && EditorUndoRedoManager::get_singleton()->is_history_unsaved(entry.value.history)) {
			_save_document(entry.key);
		}
	}
}

void AnimationTreeEditorPlugin::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		connect("scene_closed", callable_mp(this, &AnimationTreeEditorPlugin::_scene_closed));
		get_tree()->connect("node_removed", callable_mp(this, &AnimationTreeEditorPlugin::_scene_node_removed));
	}
	if (p_what == NOTIFICATION_EXIT_TREE) {
		EditorInspector::remove_inspector_plugin(inspector_plugin);
		_return_to_scene();
		Vector<String> paths;
		for (const KeyValue<String, Document> &entry : documents) {
			paths.push_back(entry.key);
		}
		for (const String &path : paths) {
			EditorSceneTabs::get_singleton()->remove_document(path);
			_close_document(path);
		}
	}
}

AnimationTreeEditorPlugin::AnimationTreeEditorPlugin() {
	anim_tree_editor = memnew(AnimationTreeEditor);
	anim_tree_editor->set_custom_minimum_size(Size2(0, 300) * EDSCALE);
	add_child(anim_tree_editor);
	// The native dock only owns parked controls. Document tabs host the visible editor.
	anim_tree_editor->hide();

	document_workspace = memnew(VBoxContainer);
	document_workspace->set_name("AnimationTreeDocumentWorkspace");
	document_workspace->hide();
	// Keep the host owned by the editor even when no resource documents are open.
	Node3DEditor::get_singleton()->show_document_workspace(document_workspace);
	Node3DEditor::get_singleton()->show_document_workspace(nullptr);
	HBoxContainer *header = memnew(HBoxContainer);
	document_workspace->add_child(header);
	document_title = memnew(Label);
	document_title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	document_title->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	header->add_child(document_title);
	node_properties_button = memnew(Button(TTR("Node Properties")));
	header->add_child(node_properties_button);
	node_properties_button->connect(SceneStringName(pressed), callable_mp(this, &AnimationTreeEditorPlugin::_inspect_node_properties));
	Button *graph_properties = memnew(Button(TTR("Graph Properties")));
	header->add_child(graph_properties);
	graph_properties->connect(SceneStringName(pressed), callable_mp(this, &AnimationTreeEditorPlugin::_inspect_graph_properties));
	header->add_child(memnew(Label(TTR("Skeleton"))));
	skeleton_picker = memnew(EditorResourcePicker);
	skeleton_picker->set_base_type("SkeletonDefinition");
	skeleton_picker->set_custom_minimum_size(Size2(240, 0) * EDSCALE);
	header->add_child(skeleton_picker);
	skeleton_picker->connect("resource_changed", callable_mp(this, &AnimationTreeEditorPlugin::_skeleton_selected));
	Button *save = memnew(Button(TTR("Save")));
	save->connect(SceneStringName(pressed), callable_mp(this, &AnimationTreeEditorPlugin::_save_current_document));
	header->add_child(save);
	HBoxContainer *storage = memnew(HBoxContainer);
	document_workspace->add_child(storage);
	storage_label = memnew(Label);
	storage_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	storage_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	storage->add_child(storage_label);
	choose_shared_button = memnew(Button(TTR("Choose Shared Graph...")));
	storage->add_child(choose_shared_button);
	choose_shared_button->connect(SceneStringName(pressed), callable_mp(this, &AnimationTreeEditorPlugin::_choose_shared).bind(ObjectID()));
	save_shared_button = memnew(Button(TTR("Save as Shared Graph...")));
	storage->add_child(save_shared_button);
	save_shared_button->connect(SceneStringName(pressed), callable_mp(this, &AnimationTreeEditorPlugin::_save_shared_as).bind(ObjectID()));
	embed_button = memnew(Button(TTR("Make Embedded Copy")));
	storage->add_child(embed_button);
	embed_button->connect(SceneStringName(pressed), callable_mp(this, &AnimationTreeEditorPlugin::_embed_graph).bind(ObjectID()));
	shared_file = memnew(EditorFileDialog);
	shared_file->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
	shared_file->add_filter("*.animtree", TTR("Animation Graph"));
	shared_file->connect("file_selected", callable_mp(this, &AnimationTreeEditorPlugin::_shared_file_selected));
	add_child(shared_file);
	inspector_plugin.instantiate(this);
	EditorInspector::add_inspector_plugin(inspector_plugin);
	graph_host = memnew(VBoxContainer);
	graph_host->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	document_workspace->add_child(graph_host);
	document_workspace->connect(SceneStringName(visibility_changed), callable_mp(this, &AnimationTreeEditorPlugin::_document_visibility_changed));

	save_confirmation = memnew(ConfirmationDialog);
	save_confirmation->set_name("AnimationTreeDocumentSaveDialog");
	save_confirmation->set_title(TTR("Unsaved Changes"));
	save_confirmation->set_text(TTR("Save the current animation tree before continuing?"));
	save_confirmation->set_ok_button_text(TTR("Save"));
	save_confirmation->add_button(TTR("Discard"), true, "discard");
	save_confirmation->connect(SceneStringName(confirmed), callable_mp(this, &AnimationTreeEditorPlugin::_save_and_navigate));
	save_confirmation->connect("custom_action", callable_mp(this, &AnimationTreeEditorPlugin::_discard_and_navigate));
	save_confirmation->connect(SNAME("canceled"), callable_mp(this, &AnimationTreeEditorPlugin::_cancel_navigation));
	EditorNode::get_singleton()->get_gui_base()->add_child(save_confirmation);
	EditorInterface::get_singleton()->connect("editor_document_restore_requested", callable_mp(this, &AnimationTreeEditorPlugin::_restore_document));
	EditorInterface::get_singleton()->connect("scene_document_selected", callable_mp(this, &AnimationTreeEditorPlugin::_return_to_scene));
	EditorUndoRedoManager::get_singleton()->connect("history_changed", callable_mp(this, &AnimationTreeEditorPlugin::_history_changed));
	EditorUndoRedoManager::get_singleton()->connect("version_changed", callable_mp(this, &AnimationTreeEditorPlugin::_history_changed));
}

void AnimationTreeEditorPlugin::_save_current_document() {
	if (documents.has(current_document)) {
		_save_document(current_document);
	}
}

void AnimationTreeEditorPlugin::_cancel_navigation() {
	pending_navigation = Callable();
	pending_document = String();
}

Ref<SkeletonDefinition> AnimationTreeEditorPlugin::_graph_skeleton(const Ref<AnimationRootNode> &p_graph) {
	String path = p_graph->get_meta("_skeleton_path", String());
	const int64_t uid = p_graph->get_meta("_skeleton_uid", ResourceUID::INVALID_ID);
	if (uid != ResourceUID::INVALID_ID && ResourceUID::get_singleton()->has_id(uid)) {
		path = ResourceUID::get_singleton()->get_id_path(uid);
	}
	return path.is_empty() ? Ref<SkeletonDefinition>() : Ref<SkeletonDefinition>(ResourceLoader::load(path));
}

static Node *animation_graph_owner(AnimationTree *p_tree) {
	for (const Variant &value : EditorInterface::get_singleton()->get_open_scene_roots()) {
		Node *root = Object::cast_to<Node>(value);
		if (root == p_tree || root->is_ancestor_of(p_tree)) {
			return root;
		}
	}
	return nullptr;
}

static Ref<SkeletonDefinition> animation_graph_player_skeleton(AnimationTree *p_tree) {
	AnimationPlayer *player = Object::cast_to<AnimationPlayer>(p_tree->get_node_or_null(p_tree->get_animation_player()));
	return player ? player->get_skeleton_definition() : Ref<SkeletonDefinition>();
}

static Error save_animation_graph_binding(AnimationTree *p_tree, Node *p_owner, const Ref<AnimationRootNode> &p_graph) {
	EditorUndoRedoManager *undo = EditorUndoRedoManager::get_singleton();
	const int history = EditorNode::get_editor_data().get_scene_history_id_from_path(p_owner->get_scene_file_path());
	undo->create_action_for_history(TTR("Change Animation Graph Binding"), history);
	undo->add_do_property(p_tree, "tree_root", p_graph);
	undo->add_undo_property(p_tree, "tree_root", p_tree->get_root_animation_node());
	undo->commit_action();
	Error error = EditorInterface::get_singleton()->save_scene_root(p_owner);
	if (error != OK) {
		undo->undo_history(history);
	}
	return error;
}

Error AnimationTreeEditorPlugin::use_shared_graph(AnimationTree *p_tree, const String &p_path) {
	ERR_FAIL_NULL_V(p_tree, ERR_INVALID_PARAMETER);
	Node *owner = animation_graph_owner(p_tree);
	ERR_FAIL_COND_V_MSG(!owner || owner->get_scene_file_path().is_empty(), ERR_FILE_BAD_PATH, "Save the owning scene before changing graph storage.");
	Ref<AnimationRootNode> graph = ResourceLoader::load(p_path);
	ERR_FAIL_COND_V(graph.is_null(), ERR_INVALID_DATA);
	Ref<SkeletonDefinition> expected = animation_graph_player_skeleton(p_tree);
	Ref<SkeletonDefinition> actual = _graph_skeleton(graph);
	ERR_FAIL_COND_V_MSG(actual.is_valid() && actual != expected, ERR_INVALID_DATA, "The shared animation graph uses another skeleton.");
	if (actual.is_null() && expected.is_valid()) {
		graph->set_meta("_skeleton_path", expected->get_path());
		graph->set_meta("_skeleton_uid", ResourceLoader::get_resource_uid(expected->get_path()));
		Error error = ResourceSaver::save(graph);
		ERR_FAIL_COND_V(error != OK, error);
	}
	Error error = save_animation_graph_binding(p_tree, owner, graph);
	if (error != OK) {
		return error;
	}
	open_node_document(p_tree);
	return OK;
}

Error AnimationTreeEditorPlugin::save_shared_graph(AnimationTree *p_tree, const String &p_path) {
	ERR_FAIL_NULL_V(p_tree, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(!p_path.has_extension("animtree"), ERR_INVALID_PARAMETER);
	Node *owner = animation_graph_owner(p_tree);
	ERR_FAIL_COND_V_MSG(!owner || owner->get_scene_file_path().is_empty(), ERR_FILE_BAD_PATH, "Save the owning scene before changing graph storage.");
	Ref<AnimationRootNode> graph = p_tree->get_root_animation_node();
	ERR_FAIL_COND_V(graph.is_null(), ERR_INVALID_DATA);
	if (documents.has(graph->get_path())) {
		graph = documents[graph->get_path()].root;
	}
	Ref<AnimationRootNode> copy = graph->duplicate(true);
	Ref<SkeletonDefinition> definition = animation_graph_player_skeleton(p_tree);
	if (definition.is_valid()) {
		copy->set_meta("_skeleton_path", definition->get_path());
		copy->set_meta("_skeleton_uid", ResourceLoader::get_resource_uid(definition->get_path()));
	}
	Error error = ResourceSaver::save(copy, p_path);
	ERR_FAIL_COND_V(error != OK, error);
	Ref<AnimationRootNode> cached = ResourceCache::get_ref(p_path);
	if (cached.is_valid()) {
		cached->copy_from(copy);
		cached->emit_changed();
		copy = cached;
	} else {
		copy->set_path(p_path, true);
	}
	return use_shared_graph(p_tree, p_path);
}

Error AnimationTreeEditorPlugin::make_graph_embedded(AnimationTree *p_tree) {
	ERR_FAIL_NULL_V(p_tree, ERR_INVALID_PARAMETER);
	Node *owner = animation_graph_owner(p_tree);
	ERR_FAIL_COND_V_MSG(!owner || owner->get_scene_file_path().is_empty(), ERR_FILE_BAD_PATH, "Save the owning scene before changing graph storage.");
	Ref<AnimationRootNode> graph = p_tree->get_root_animation_node();
	ERR_FAIL_COND_V(graph.is_null(), ERR_INVALID_DATA);
	if (documents.has(graph->get_path())) {
		graph = documents[graph->get_path()].root;
	}
	Ref<AnimationRootNode> copy = graph->duplicate(true);
	copy->remove_meta("_skeleton_path");
	copy->remove_meta("_skeleton_uid");
	Error error = save_animation_graph_binding(p_tree, owner, copy);
	if (error != OK) {
		return error;
	}
	open_node_document(p_tree);
	return OK;
}

void AnimationTreeEditorPlugin::_choose_shared(ObjectID p_tree) {
	if (p_tree.is_null()) {
		p_tree = action_tree;
	}
	if (!ObjectDB::get_instance(p_tree)) {
		EditorNode::get_singleton()->show_warning(TTR("Open the graph from an AnimationTree node to change its binding."));
		return;
	}
	_navigate_document(callable_mp(this, &AnimationTreeEditorPlugin::_open_shared_picker).bind(p_tree));
}

void AnimationTreeEditorPlugin::_open_shared_picker(ObjectID p_tree) {
	TypedArray<StringName> types;
	types.push_back("AnimationRootNode");
	EditorInterface::get_singleton()->popup_quick_open(callable_mp(this, &AnimationTreeEditorPlugin::_shared_selected).bind(p_tree), types);
}

void AnimationTreeEditorPlugin::_shared_selected(const String &p_path, ObjectID p_tree) {
	if (p_path.is_empty()) {
		return;
	}
	AnimationTree *tree = Object::cast_to<AnimationTree>(ObjectDB::get_instance(p_tree));
	if (!tree) {
		return;
	}
	Error error = use_shared_graph(tree, p_path);
	if (error != OK) {
		EditorNode::get_singleton()->show_warning(TTR("The shared graph must use the same skeleton, and the owning scene must be saved."));
	}
}

void AnimationTreeEditorPlugin::_save_shared_as(ObjectID p_tree) {
	if (p_tree.is_null()) {
		p_tree = action_tree;
	}
	AnimationTree *tree = Object::cast_to<AnimationTree>(ObjectDB::get_instance(p_tree));
	if (!tree) {
		EditorNode::get_singleton()->show_warning(TTR("Open the graph from an AnimationTree node to change its binding."));
		return;
	}
	_navigate_document(callable_mp(this, &AnimationTreeEditorPlugin::_open_shared_save_dialog).bind(p_tree));
}

void AnimationTreeEditorPlugin::_open_shared_save_dialog(ObjectID p_tree) {
	AnimationTree *target = Object::cast_to<AnimationTree>(ObjectDB::get_instance(p_tree));
	if (!target) {
		return;
	}
	action_tree = p_tree;
	Node *root = animation_graph_owner(target);
	shared_file->set_current_path(root->get_scene_file_path().get_basename() + ".animtree");
	shared_file->popup_file_dialog();
}

void AnimationTreeEditorPlugin::_shared_file_selected(const String &p_path) {
	AnimationTree *tree = Object::cast_to<AnimationTree>(ObjectDB::get_instance(action_tree));
	if (tree && save_shared_graph(tree, p_path) != OK) {
		EditorNode::get_singleton()->show_warning(TTR("Could not save the shared animation graph."));
	}
	EditorFileSystem::get_singleton()->scan();
}

void AnimationTreeEditorPlugin::_embed_graph(ObjectID p_tree) {
	if (p_tree.is_null()) {
		p_tree = action_tree;
	}
	_navigate_document(callable_mp(this, &AnimationTreeEditorPlugin::_make_embedded).bind(p_tree));
}

void AnimationTreeEditorPlugin::_make_embedded(ObjectID p_tree) {
	AnimationTree *tree = Object::cast_to<AnimationTree>(ObjectDB::get_instance(p_tree));
	if (tree && make_graph_embedded(tree) != OK) {
		EditorNode::get_singleton()->show_warning(TTR("Could not save the embedded animation graph."));
	}
}

Control *AnimationTreeEditorPlugin::create_graph_actions(AnimationTree *p_tree) {
	VBoxContainer *controls = memnew(VBoxContainer);
	Ref<AnimationRootNode> graph = p_tree->get_root_animation_node();
	controls->add_child(memnew(Label(graph.is_valid() && !graph->is_built_in() ? TTR("Shared Animation Graph") + " " + graph->get_path().get_file() : TTR("Embedded in Scene"))));
	Button *edit = memnew(Button(TTR("Edit Animation Graph")));
	edit->connect(SceneStringName(pressed), callable_mp(this, &AnimationTreeEditorPlugin::open_node_document).bind(p_tree, true));
	controls->add_child(edit);
	Button *choose = memnew(Button(TTR("Choose Shared Graph...")));
	choose->connect(SceneStringName(pressed), callable_mp(this, &AnimationTreeEditorPlugin::_choose_shared).bind(p_tree->get_instance_id()));
	controls->add_child(choose);
	Button *save = memnew(Button(TTR("Save as Shared Graph...")));
	save->connect(SceneStringName(pressed), callable_mp(this, &AnimationTreeEditorPlugin::_save_shared_as).bind(p_tree->get_instance_id()));
	controls->add_child(save);
	Button *embed = memnew(Button(TTR("Make Embedded Copy")));
	embed->connect(SceneStringName(pressed), callable_mp(this, &AnimationTreeEditorPlugin::_embed_graph).bind(p_tree->get_instance_id()));
	controls->add_child(embed);
	return controls;
}

bool AnimationGraphInspectorPlugin::can_handle(Object *p_object) {
	return Object::cast_to<AnimationTree>(p_object);
}

void AnimationGraphInspectorPlugin::parse_begin(Object *p_object) {
	add_custom_control(editor->create_graph_actions(Object::cast_to<AnimationTree>(p_object)));
}

void AnimationTreeEditorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("open_node_document", "tree", "activate"), &AnimationTreeEditorPlugin::open_node_document, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("use_shared_graph", "tree", "path"), &AnimationTreeEditorPlugin::use_shared_graph);
	ClassDB::bind_method(D_METHOD("save_shared_graph", "tree", "path"), &AnimationTreeEditorPlugin::save_shared_graph);
	ClassDB::bind_method(D_METHOD("make_graph_embedded", "tree"), &AnimationTreeEditorPlugin::make_graph_embedded);
}

void AnimationTreeEditorPlugin::_update_storage_actions() {
	AnimationTree *target = Object::cast_to<AnimationTree>(ObjectDB::get_instance(action_tree));
	node_properties_button->set_visible(target != nullptr);
	choose_shared_button->set_visible(target != nullptr);
	save_shared_button->set_visible(target != nullptr);
	embed_button->set_visible(target != nullptr);
	if (target && documents.has(current_document) && documents[current_document].scene_tree.is_null()) {
		Node *owner = animation_graph_owner(target);
		if (owner) {
			storage_label->set_text(TTR("Shared Animation Graph") + " / " + owner->get_scene_file_path().get_file());
		}
	}
}
