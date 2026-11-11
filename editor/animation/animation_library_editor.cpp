/**************************************************************************/
/*  animation_library_editor.cpp                                          */
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

#include "animation_library_editor.h"

#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/undo_redo.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/file_system/editor_paths.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/animation/animation_mixer.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/margin_container.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/packed_scene.h"

// Temporary inverse operations are only used to roll back a failed disk write.
// They are never registered in the editor's user-visible undo history.
struct AnimationManagementAction {
	UndoRedo *action = memnew(UndoRedo);
	~AnimationManagementAction() { memdelete(action); }
};

void AnimationLibraryEditor::set_animation_mixer(Object *p_mixer) {
	mixer = Object::cast_to<AnimationMixer>(p_mixer);
}

void AnimationLibraryEditor::_add_library() {
	add_library_name->set_text("");
	add_library_dialog->popup_centered();
	add_library_name->grab_focus();
	adding_animation = false;
	adding_animation_to_library = StringName();
	_add_library_validate("");
}

void AnimationLibraryEditor::_add_library_validate(const String &p_name) {
	String error;

	if (adding_animation) {
		Ref<AnimationLibrary> al = mixer->get_animation_library(adding_animation_to_library);
		ERR_FAIL_COND(al.is_null());
		if (p_name == "") {
			error = TTR("Animation name can't be empty.");
		} else if (!AnimationLibrary::is_valid_animation_name(p_name)) {
			error = TTR("Animation name contains invalid characters: '/', ':', ',' or '['.");
		} else if (al->has_animation(p_name)) {
			error = TTR("Animation with the same name already exists.");
		}
	} else {
		if (p_name == "" && mixer->has_animation_library("")) {
			error = TTR("Enter a library name.");
		} else if (!AnimationLibrary::is_valid_library_name(p_name)) {
			error = TTR("Library name contains invalid characters: '/', ':', ',' or '['.");
		} else if (mixer->has_animation_library(p_name)) {
			error = TTR("Library with the same name already exists.");
		}
	}

	if (error != "") {
		add_library_validate->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("error_color"), EditorStringName(Editor)));
		add_library_validate->set_text(error);
		add_library_dialog->get_ok_button()->set_disabled(true);
	} else {
		if (adding_animation) {
			add_library_validate->set_text(TTR("Animation name is valid."));
		} else {
			if (p_name == "") {
				add_library_validate->set_text(TTR("Global library will be created."));
			} else {
				add_library_validate->set_text(TTR("Library name is valid."));
			}
		}
		add_library_validate->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("success_color"), EditorStringName(Editor)));
		add_library_dialog->get_ok_button()->set_disabled(false);
	}
}

void AnimationLibraryEditor::_add_library_confirm() {
	if (adding_animation) {
		String anim_name = add_library_name->get_text();
		AnimationManagementAction transaction;
		UndoRedo *undo_redo = transaction.action;

		Ref<AnimationLibrary> al = mixer->get_animation_library(adding_animation_to_library);
		ERR_FAIL_COND(al.is_null());

		Ref<Animation> anim;
		anim.instantiate();

		undo_redo->create_action(vformat(TTR("Add Animation to Library: %s"), anim_name));
		undo_redo->add_do_method(Callable(al.ptr(), "add_animation").bind(anim_name, anim));
		undo_redo->add_undo_method(Callable(al.ptr(), "remove_animation").bind(anim_name));
		undo_redo->add_do_method(Callable(this, "_update_editor").bind(mixer));
		undo_redo->add_undo_method(Callable(this, "_update_editor").bind(mixer));
		_commit_management(undo_redo);

	} else {
		String lib_name = add_library_name->get_text();
		AnimationManagementAction transaction;
		UndoRedo *undo_redo = transaction.action;

		Ref<AnimationLibrary> al;
		al.instantiate();

		undo_redo->create_action(vformat(TTR("Add Animation Library: %s"), lib_name));
		undo_redo->add_do_method(Callable(mixer, "add_animation_library").bind(lib_name, al));
		undo_redo->add_undo_method(Callable(mixer, "remove_animation_library").bind(lib_name));
		undo_redo->add_do_method(Callable(this, "_update_editor").bind(mixer));
		undo_redo->add_undo_method(Callable(this, "_update_editor").bind(mixer));
		_commit_management(undo_redo);
	}
}

void AnimationLibraryEditor::_load_library() {
	List<String> extensions;
	ResourceLoader::get_recognized_extensions_for_type("AnimationLibrary", &extensions);

	file_dialog->set_title(TTR("Load Animation"));
	file_dialog->clear_filters();
	for (const String &K : extensions) {
		file_dialog->add_filter("*." + K);
	}

	file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILES);
	file_dialog->set_current_file("");
	file_dialog->popup_centered_ratio();

	file_dialog_action = FILE_DIALOG_ACTION_OPEN_LIBRARY;
}

void AnimationLibraryEditor::_file_popup_selected(int p_id) {
	Ref<AnimationLibrary> al = mixer->get_animation_library(file_dialog_library);
	Ref<Animation> anim;
	if (file_dialog_animation != StringName()) {
		anim = al->get_animation(file_dialog_animation);
		ERR_FAIL_COND(anim.is_null());
	}
	switch (p_id) {
		case FILE_MENU_SAVE_LIBRARY: {
			if (al->get_path().is_resource_file() && !FileAccess::exists(al->get_path() + ".import")) {
				EditorNode::get_singleton()->save_resource(al);
				break;
			}
			[[fallthrough]];
		}
		case FILE_MENU_SAVE_AS_LIBRARY: {
			// Check if we're allowed to save this
			{
				String al_path = al->get_path();
				if (!al_path.is_resource_file()) {
					int srpos = al_path.find("::");
					if (srpos != -1) {
						String base = al_path.substr(0, srpos);
						if (!get_tree()->get_edited_scene_root() || get_tree()->get_edited_scene_root()->get_scene_file_path() != base) {
							error_dialog->set_text(TTR("This animation library can't be saved because it does not belong to the edited scene. Make it unique first."));
							error_dialog->popup_centered();
							return;
						}
					}
				} else {
					if (FileAccess::exists(al_path + ".import")) {
						error_dialog->set_text(TTR("This animation library can't be saved because it was imported from another file. Make it unique first."));
						error_dialog->popup_centered();
						return;
					}
				}
			}

			file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
			file_dialog->set_title(TTR("Save Library"));
			if (al->get_path().is_resource_file()) {
				file_dialog->set_current_path(al->get_path());
			} else {
				file_dialog->set_current_file(String(file_dialog_library) + ".animlib");
			}
			file_dialog->clear_filters();
			List<String> exts;
			ResourceLoader::get_recognized_extensions_for_type("AnimationLibrary", &exts);
			for (const String &K : exts) {
				file_dialog->add_filter("*." + K);
			}

			file_dialog->popup_centered_ratio();
			file_dialog_action = FILE_DIALOG_ACTION_SAVE_LIBRARY;
		} break;
		case FILE_MENU_MAKE_LIBRARY_UNIQUE: {
			StringName lib_name = file_dialog_library;
			LocalVector<StringName> animation_list;

			Ref<AnimationLibrary> ald = memnew(AnimationLibrary);
			al->get_animation_list(&animation_list);
			for (const StringName &animation_name : animation_list) {
				Ref<Animation> animation = al->get_animation(animation_name);
				if (EditorNode::get_singleton()->is_resource_read_only(animation)) {
					animation = animation->duplicate();
				}
				ald->add_animation(animation_name, animation);
			}

			AnimationManagementAction transaction;
			UndoRedo *undo_redo = transaction.action;
			undo_redo->create_action(vformat(TTR("Make Animation Library Unique: %s"), lib_name));
			undo_redo->add_do_method(Callable(mixer, "remove_animation_library").bind(lib_name));
			undo_redo->add_do_method(Callable(mixer, "add_animation_library").bind(lib_name, ald));
			undo_redo->add_undo_method(Callable(mixer, "remove_animation_library").bind(lib_name));
			undo_redo->add_undo_method(Callable(mixer, "add_animation_library").bind(lib_name, al));
			undo_redo->add_do_method(Callable(this, "_update_editor").bind(mixer));
			undo_redo->add_undo_method(Callable(this, "_update_editor").bind(mixer));
			_commit_management(undo_redo);

			update_tree();

		} break;
		case FILE_MENU_EDIT_LIBRARY: {
			EditorNode::get_singleton()->push_item(al.ptr());
		} break;

		case FILE_MENU_SAVE_ANIMATION: {
			if (anim->get_path().is_resource_file() && !FileAccess::exists(anim->get_path() + ".import")) {
				EditorNode::get_singleton()->save_resource(anim);
				break;
			}
			[[fallthrough]];
		}
		case FILE_MENU_SAVE_AS_ANIMATION: {
			// Check if we're allowed to save this
			{
				String anim_path = al->get_path();
				if (!anim_path.is_resource_file()) {
					int srpos = anim_path.find("::");
					if (srpos != -1) {
						String base = anim_path.substr(0, srpos);
						if (!get_tree()->get_edited_scene_root() || get_tree()->get_edited_scene_root()->get_scene_file_path() != base) {
							error_dialog->set_text(TTR("This animation can't be saved because it does not belong to the edited scene. Make it unique first."));
							error_dialog->popup_centered();
							return;
						}
					}
				} else {
					if (FileAccess::exists(anim_path + ".import")) {
						error_dialog->set_text(TTR("This animation can't be saved because it was imported from another file. Make it unique first."));
						error_dialog->popup_centered();
						return;
					}
				}
			}

			file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
			file_dialog->set_title(TTR("Save Animation"));
			if (anim->get_path().is_resource_file()) {
				file_dialog->set_current_path(anim->get_path());
			} else {
				file_dialog->set_current_file(String(file_dialog_animation) + ".anim");
			}
			file_dialog->clear_filters();
			List<String> exts;
			ResourceLoader::get_recognized_extensions_for_type("Animation", &exts);
			for (const String &K : exts) {
				file_dialog->add_filter("*." + K);
			}

			file_dialog->popup_centered_ratio();
			file_dialog_action = FILE_DIALOG_ACTION_SAVE_ANIMATION;
		} break;
		case FILE_MENU_MAKE_ANIMATION_UNIQUE: {
			StringName anim_name = file_dialog_animation;

			Ref<Animation> animd = anim->duplicate();

			AnimationManagementAction transaction;
			UndoRedo *undo_redo = transaction.action;
			undo_redo->create_action(vformat(TTR("Make Animation Unique: %s"), anim_name));
			undo_redo->add_do_method(Callable(al.ptr(), "remove_animation").bind(anim_name));
			undo_redo->add_do_method(Callable(al.ptr(), "add_animation").bind(anim_name, animd));
			undo_redo->add_undo_method(Callable(al.ptr(), "remove_animation").bind(anim_name));
			undo_redo->add_undo_method(Callable(al.ptr(), "add_animation").bind(anim_name, anim));
			undo_redo->add_do_method(Callable(this, "_update_editor").bind(mixer));
			undo_redo->add_undo_method(Callable(this, "_update_editor").bind(mixer));
			_commit_management(undo_redo);

			update_tree();
		} break;
		case FILE_MENU_EDIT_ANIMATION: {
			EditorNode::get_singleton()->push_item(anim.ptr());
		} break;
	}
}

void AnimationLibraryEditor::_load_file(const String &p_path) {
	switch (file_dialog_action) {
		case FILE_DIALOG_ACTION_SAVE_LIBRARY: {
			Ref<AnimationLibrary> al = mixer->get_animation_library(file_dialog_library);
			String prev_path = al->get_path();
			EditorNode::get_singleton()->save_resource_in_path(al, p_path);

			if (al->get_path() != prev_path) { // Save successful.
				AnimationManagementAction transaction;
				UndoRedo *undo_redo = transaction.action;

				undo_redo->create_action(vformat(TTR("Save Animation library to File: %s"), file_dialog_library));
				undo_redo->add_do_method(Callable(al.ptr(), "set_path").bind(al->get_path()));
				undo_redo->add_undo_method(Callable(al.ptr(), "set_path").bind(prev_path));
				undo_redo->add_do_method(Callable(this, "_update_editor").bind(mixer));
				undo_redo->add_undo_method(Callable(this, "_update_editor").bind(mixer));
				_commit_management(undo_redo);
			}

		} break;
		case FILE_DIALOG_ACTION_SAVE_ANIMATION: {
			Ref<AnimationLibrary> al = mixer->get_animation_library(file_dialog_library);
			Ref<Animation> anim;
			if (file_dialog_animation != StringName()) {
				anim = al->get_animation(file_dialog_animation);
				ERR_FAIL_COND(anim.is_null());
			}
			String prev_path = anim->get_path();
			EditorNode::get_singleton()->save_resource_in_path(anim, p_path);
			if (anim->get_path() != prev_path) { // Save successful.
				AnimationManagementAction transaction;
				UndoRedo *undo_redo = transaction.action;

				undo_redo->create_action(vformat(TTR("Save Animation to File: %s"), file_dialog_animation));
				undo_redo->add_do_method(Callable(anim.ptr(), "set_path").bind(anim->get_path()));
				undo_redo->add_undo_method(Callable(anim.ptr(), "set_path").bind(prev_path));
				undo_redo->add_do_method(Callable(this, "_update_editor").bind(mixer));
				undo_redo->add_undo_method(Callable(this, "_update_editor").bind(mixer));
				_commit_management(undo_redo);
			}
		} break;
		default: {
		}
	}
}

void AnimationLibraryEditor::_load_files(const PackedStringArray &p_paths) {
	AnimationManagementAction transaction;
	UndoRedo *undo_redo = transaction.action;
	bool has_created_action = false;
	bool show_error_diag = false;
	List<String> name_list;

	switch (file_dialog_action) {
		case FILE_DIALOG_ACTION_OPEN_LIBRARY: {
			for (const String &path : p_paths) {
				const Ref<Resource> res = ResourceLoader::load(path);
				const Ref<AnimationLibrary> anim_library = res;
				if (anim_library.is_null()) {
					show_error_diag = true;
					const Ref<PackedScene> scene = res;
					if (scene.is_valid()) {
						error_dialog->set_text(TTR(U"The file you selected is an imported scene from a 3D model such as glTF or FBX.\n\nIn Godot, 3D models can be imported as either scenes or animation libraries, which is why they show up here.\n\nIf you want to use animations from this 3D model, open the Advanced Import Settings\ndialog and save the animations using Actions... → Set Animation Save Paths,\nor import the whole scene as a single AnimationLibrary in the Import dock."));
					} else {
						error_dialog->set_text(TTR("The file you selected is not a valid AnimationLibrary.\n\nIf the animations you want are inside of this file, save them to a separate file first."));
					}
					continue;
				}

				LocalVector<StringName> libs;
				mixer->get_animation_library_list(&libs);
				bool is_already_added = false;
				for (const StringName &K : libs) {
					if (mixer->get_animation_library(K) == anim_library) {
						// Prioritize the "invalid" error message.
						if (!show_error_diag) {
							show_error_diag = true;
							error_dialog->set_text(TTR("Some of the selected libraries were already added to the mixer."));
						}

						is_already_added = true;
						break;
					}
				}

				if (is_already_added) {
					continue;
				}

				String name = AnimationLibrary::validate_library_name(path.get_file().get_basename());
				int attempt = 1;
				while (bool(mixer->has_animation_library(name)) || name_list.find(name)) {
					attempt++;
					name = path.get_file().get_basename() + " " + itos(attempt);
				}
				name_list.push_back(name);

				if (!has_created_action) {
					has_created_action = true;
					undo_redo->create_action(p_paths.size() > 1 ? TTR("Add Animation Libraries") : vformat(TTR("Add Animation Library: %s"), name));
				}
				undo_redo->add_do_method(Callable(mixer, "add_animation_library").bind(name, anim_library));
				undo_redo->add_undo_method(Callable(mixer, "remove_animation_library").bind(name));
			}
		} break;
		case FILE_DIALOG_ACTION_OPEN_ANIMATION: {
			Ref<AnimationLibrary> al = mixer->get_animation_library(adding_animation_to_library);
			for (const String &path : p_paths) {
				Ref<Animation> anim = ResourceLoader::load(path);
				if (anim.is_null()) {
					show_error_diag = true;
					error_dialog->set_text(TTR("Some Animation files were invalid."));
					continue;
				}

				LocalVector<StringName> anims;
				al->get_animation_list(&anims);
				bool is_already_added = false;
				for (const StringName &K : anims) {
					if (al->get_animation(K) == anim) {
						// Prioritize the "invalid" error message.
						if (!show_error_diag) {
							show_error_diag = true;
							error_dialog->set_text(TTR("Some of the selected animations were already added to the library."));
						}

						is_already_added = true;
						break;
					}
				}

				if (is_already_added) {
					continue;
				}

				String name = path.get_file().get_basename();
				int attempt = 1;
				while (al->has_animation(name) || name_list.find(name)) {
					attempt++;
					name = path.get_file().get_basename() + " " + itos(attempt);
				}
				name_list.push_back(name);

				if (!has_created_action) {
					has_created_action = true;
					undo_redo->create_action(p_paths.size() > 1 ? TTR("Load Animations into Library") : vformat(TTR("Load Animation into Library: %s"), name));
				}
				undo_redo->add_do_method(Callable(al.ptr(), "add_animation").bind(name, anim));
				undo_redo->add_undo_method(Callable(al.ptr(), "remove_animation").bind(name));
			}
		} break;
		default: {
		}
	}

	if (has_created_action) {
		undo_redo->add_do_method(Callable(this, "_update_editor").bind(mixer));
		undo_redo->add_undo_method(Callable(this, "_update_editor").bind(mixer));
		_commit_management(undo_redo);
	}

	if (show_error_diag) {
		error_dialog->popup_centered();
	}
}

void AnimationLibraryEditor::_item_renamed() {
	TreeItem *ti = tree->get_edited();
	String text = ti->get_text(0);
	String old_text = ti->get_metadata(0);
	bool restore_text = false;
	AnimationManagementAction transaction;
	UndoRedo *undo_redo = transaction.action;

	if (String(text).contains_char('/') || String(text).contains_char(':') || String(text).contains_char(',') || String(text).contains_char('[')) {
		restore_text = true;
	} else {
		if (ti->get_parent() == tree->get_root()) {
			// Renamed library

			if (mixer->has_animation_library(text)) {
				restore_text = true;
			} else {
				undo_redo->create_action(vformat(TTR("Rename Animation Library: %s"), text));
				undo_redo->add_do_method(Callable(mixer, "rename_animation_library").bind(old_text, text));
				undo_redo->add_undo_method(Callable(mixer, "rename_animation_library").bind(text, old_text));
				undo_redo->add_do_method(Callable(this, "_update_editor").bind(mixer));
				undo_redo->add_undo_method(Callable(this, "_update_editor").bind(mixer));
				updating = true;
				const bool saved = _commit_management(undo_redo);
				updating = false;
				if (!saved) {
					ti->set_text(0, old_text);
					return;
				}
				ti->set_metadata(0, text);
				if (text == "") {
					ti->set_suffix(0, TTR("[Global]"));
				} else {
					ti->set_suffix(0, "");
				}
			}
		} else {
			// Renamed anim
			StringName library = ti->get_parent()->get_metadata(0);
			Ref<AnimationLibrary> al = mixer->get_animation_library(library);

			if (al.is_valid()) {
				if (al->has_animation(text)) {
					restore_text = true;
				} else {
					undo_redo->create_action(vformat(TTR("Rename Animation: %s"), text));
					undo_redo->add_do_method(Callable(al.ptr(), "rename_animation").bind(old_text, text));
					undo_redo->add_undo_method(Callable(al.ptr(), "rename_animation").bind(text, old_text));
					undo_redo->add_do_method(Callable(this, "_update_editor").bind(mixer));
					undo_redo->add_undo_method(Callable(this, "_update_editor").bind(mixer));
					updating = true;
					const bool saved = _commit_management(undo_redo);
					updating = false;
					if (!saved) {
						ti->set_text(0, old_text);
						return;
					}

					ti->set_metadata(0, text);
				}
			} else {
				restore_text = true;
			}
		}
	}

	if (restore_text) {
		ti->set_text(0, old_text);
	}

	_save_mixer_lib_folding(ti);
}

void AnimationLibraryEditor::_button_pressed(TreeItem *p_item, int p_column, int p_id, MouseButton p_button) {
	if (p_item->get_parent() == tree->get_root()) {
		// Library
		StringName lib_name = p_item->get_metadata(0);
		Ref<AnimationLibrary> al = mixer->get_animation_library(lib_name);
		switch (p_id) {
			case LIB_BUTTON_ADD: {
				add_library_name->set_text("");
				add_library_dialog->popup_centered();
				add_library_name->grab_focus();
				adding_animation = true;
				adding_animation_to_library = p_item->get_metadata(0);
				_add_library_validate("");
			} break;
			case LIB_BUTTON_LOAD: {
				adding_animation_to_library = p_item->get_metadata(0);
				List<String> extensions;
				ResourceLoader::get_recognized_extensions_for_type("Animation", &extensions);

				file_dialog->clear_filters();
				for (const String &K : extensions) {
					file_dialog->add_filter("*." + K);
				}

				file_dialog->set_title(TTR("Load Animation"));
				file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILES);
				file_dialog->set_current_file("");
				file_dialog->popup_centered_ratio();

				file_dialog_action = FILE_DIALOG_ACTION_OPEN_ANIMATION;

			} break;
			case LIB_BUTTON_PASTE: {
				Ref<Animation> anim = EditorSettings::get_singleton()->get_resource_clipboard();
				if (anim.is_null()) {
					error_dialog->set_text(TTR("No animation resource in clipboard!"));
					error_dialog->popup_centered();
					return;
				}

				if (!anim->get_path().is_resource_file()) {
					anim = anim->duplicate(); // Users simply dont care about referencing, so making a copy works better here.
				}

				String base_name;
				if (anim->get_name() != "") {
					base_name = anim->get_name();
				} else {
					base_name = TTR("Pasted Animation");
				}

				String name = base_name;
				int attempt = 1;
				while (al->has_animation(name)) {
					attempt++;
					name = base_name + " (" + itos(attempt) + ")";
				}

				AnimationManagementAction transaction;
				UndoRedo *undo_redo = transaction.action;

				undo_redo->create_action(vformat(TTR("Add Animation to Library: %s"), name));
				undo_redo->add_do_method(Callable(al.ptr(), "add_animation").bind(name, anim));
				undo_redo->add_undo_method(Callable(al.ptr(), "remove_animation").bind(name));
				undo_redo->add_do_method(Callable(this, "_update_editor").bind(mixer));
				undo_redo->add_undo_method(Callable(this, "_update_editor").bind(mixer));
				_commit_management(undo_redo);

			} break;
			case LIB_BUTTON_FILE: {
				file_popup->clear();
				file_popup->add_item(TTR("Save As"), FILE_MENU_SAVE_AS_LIBRARY);
				file_popup->add_separator();
				file_popup->add_item(TTR("Make Unique"), FILE_MENU_MAKE_LIBRARY_UNIQUE);
				file_popup->add_separator();
				file_popup->add_item(TTR("Open in Inspector"), FILE_MENU_EDIT_LIBRARY);
				Rect2 pos = tree->get_item_rect(p_item, 1, 0);
				Vector2 popup_pos = tree->get_screen_transform().xform(pos.position + Vector2(0, pos.size.height));
				file_popup->popup(Rect2(popup_pos, Size2()));

				file_dialog_animation = StringName();
				file_dialog_library = lib_name;
			} break;
			case LIB_BUTTON_DELETE: {
				AnimationManagementAction transaction;
				UndoRedo *undo_redo = transaction.action;
				undo_redo->create_action(vformat(TTR("Remove Animation Library: %s"), lib_name));
				undo_redo->add_do_method(Callable(mixer, "remove_animation_library").bind(lib_name));
				undo_redo->add_undo_method(Callable(mixer, "add_animation_library").bind(lib_name, al));
				undo_redo->add_do_method(Callable(this, "_update_editor").bind(mixer));
				undo_redo->add_undo_method(Callable(this, "_update_editor").bind(mixer));
				_commit_management(undo_redo);
			} break;
		}

	} else {
		// Animation
		StringName lib_name = p_item->get_parent()->get_metadata(0);
		StringName anim_name = p_item->get_metadata(0);
		Ref<AnimationLibrary> al = mixer->get_animation_library(lib_name);
		Ref<Animation> anim = al->get_animation(anim_name);
		ERR_FAIL_COND(anim.is_null());
		switch (p_id) {
			case ANIM_BUTTON_COPY: {
				if (anim->get_name() == "") {
					anim->set_name(anim_name); // Keep the name around
				}
				EditorSettings::get_singleton()->set_resource_clipboard(anim);
			} break;
			case ANIM_BUTTON_FILE: {
				file_popup->clear();
				file_popup->add_item(TTR("Save As"), FILE_MENU_SAVE_AS_ANIMATION);
				file_popup->add_separator();
				file_popup->add_item(TTR("Make Unique"), FILE_MENU_MAKE_ANIMATION_UNIQUE);
				file_popup->add_separator();
				file_popup->add_item(TTR("Open in Inspector"), FILE_MENU_EDIT_ANIMATION);
				Rect2 pos = tree->get_item_rect(p_item, 1, 0);
				Vector2 popup_pos = tree->get_screen_transform().xform(pos.position + Vector2(0, pos.size.height));
				file_popup->popup(Rect2(popup_pos, Size2()));

				file_dialog_animation = anim_name;
				file_dialog_library = lib_name;

			} break;
			case ANIM_BUTTON_DELETE: {
				AnimationManagementAction transaction;
				UndoRedo *undo_redo = transaction.action;
				undo_redo->create_action(vformat(TTR("Remove Animation from Library: %s"), anim_name));
				undo_redo->add_do_method(Callable(al.ptr(), "remove_animation").bind(anim_name));
				undo_redo->add_undo_method(Callable(al.ptr(), "add_animation").bind(anim_name, anim));
				undo_redo->add_do_method(Callable(this, "_update_editor").bind(mixer));
				undo_redo->add_undo_method(Callable(this, "_update_editor").bind(mixer));
				_commit_management(undo_redo);
			} break;
		}
	}
}

void AnimationLibraryEditor::update_tree() {
	if (updating) {
		return;
	}

	tree->clear();
	ERR_FAIL_NULL(mixer);

	Color ss_color = get_theme_color(SNAME("prop_subsection"), EditorStringName(Editor));

	TreeItem *root = tree->create_item();
	LocalVector<StringName> libs;
	const Vector<String> collapsed_libs = _load_mixer_libs_folding();

	mixer->get_animation_library_list(&libs);

	for (const StringName &K : libs) {
		TreeItem *libitem = tree->create_item(root);
		libitem->set_text(0, K);
		if (K == StringName()) {
			libitem->set_suffix(0, TTR("[Global]"));
		} else {
			libitem->set_suffix(0, "");
		}

		Ref<AnimationLibrary> al = mixer->get_animation_library(K);
		bool animation_library_is_foreign = false;
		String al_path = al->get_path();
		if (!al_path.is_resource_file()) {
			libitem->set_text(1, TTR("[built-in]"));
			libitem->set_tooltip_text(1, al_path);
			int srpos = al_path.find("::");
			if (srpos != -1) {
				String base = al_path.substr(0, srpos);
				if (ResourceLoader::get_resource_type(base) == "PackedScene") {
					if (!get_tree()->get_edited_scene_root() || get_tree()->get_edited_scene_root()->get_scene_file_path() != base) {
						animation_library_is_foreign = true;
						libitem->set_text(1, TTR("[foreign]"));
					}
				} else {
					if (FileAccess::exists(base + ".import")) {
						animation_library_is_foreign = true;
						libitem->set_text(1, TTR("[imported]"));
					}
				}
			}
		} else {
			if (FileAccess::exists(al_path + ".import")) {
				animation_library_is_foreign = true;
				libitem->set_text(1, TTR("[imported]"));
			} else {
				libitem->set_text(1, al_path.get_file());
			}
		}

		libitem->set_editable(0, true);
		libitem->set_metadata(0, K);
		libitem->set_icon(0, get_editor_theme_icon("AnimationLibrary"));

		libitem->add_button(0, get_editor_theme_icon("Add"), LIB_BUTTON_ADD, animation_library_is_foreign, TTR("Add animation to library."));
		libitem->add_button(0, get_editor_theme_icon("Load"), LIB_BUTTON_LOAD, animation_library_is_foreign, TTR("Load animation from file and add to library."));
		libitem->add_button(0, get_editor_theme_icon("ActionPaste"), LIB_BUTTON_PASTE, animation_library_is_foreign, TTR("Paste animation to library from clipboard."));

		libitem->add_button(1, get_editor_theme_icon("Save"), LIB_BUTTON_FILE, false, TTR("Animation library resource actions."));
		libitem->add_button(1, get_editor_theme_icon("Remove"), LIB_BUTTON_DELETE, false, TTR("Remove animation library."));

		libitem->set_custom_bg_color(0, ss_color);

		LocalVector<StringName> animations;
		al->get_animation_list(&animations);
		for (const StringName &L : animations) {
			TreeItem *anitem = tree->create_item(libitem);
			anitem->set_text(0, L);
			anitem->set_editable(0, !animation_library_is_foreign);
			anitem->set_metadata(0, L);
			anitem->set_icon(0, get_editor_theme_icon("Animation"));
			anitem->add_button(0, get_editor_theme_icon("ActionCopy"), ANIM_BUTTON_COPY, animation_library_is_foreign, TTR("Copy animation to clipboard."));

			Ref<Animation> anim = al->get_animation(L);
			String anim_path = anim->get_path();
			if (!anim_path.is_resource_file()) {
				anitem->set_text(1, TTR("[built-in]"));
				anitem->set_tooltip_text(1, anim_path);
				int srpos = anim_path.find("::");
				if (srpos != -1) {
					String base = anim_path.substr(0, srpos);
					if (ResourceLoader::get_resource_type(base) == "PackedScene") {
						if (!get_tree()->get_edited_scene_root() || get_tree()->get_edited_scene_root()->get_scene_file_path() != base) {
							anitem->set_text(1, TTR("[foreign]"));
						}
					} else {
						if (FileAccess::exists(base + ".import")) {
							anitem->set_text(1, TTR("[imported]"));
						}
					}
				}
			} else {
				if (FileAccess::exists(anim_path + ".import")) {
					anitem->set_text(1, TTR("[imported]"));
				} else {
					anitem->set_text(1, anim_path.get_file());
				}
			}

			anitem->add_button(1, get_editor_theme_icon("Save"), ANIM_BUTTON_FILE, animation_library_is_foreign, TTR("Animation resource actions."));
			anitem->add_button(1, get_editor_theme_icon("Remove"), ANIM_BUTTON_DELETE, animation_library_is_foreign, TTR("Remove animation from Library."));
		}

		if (collapsed_libs.has(String(K))) {
			libitem->set_collapsed_recursive(true);
		}
	}
}

void AnimationLibraryEditor::_save_mixer_lib_folding(TreeItem *p_item) {
	//Check if ti is a library or animation
	if (p_item->get_parent()->get_parent() != nullptr) {
		return;
	}

	Ref<ConfigFile> config;
	config.instantiate();

	String path = EditorPaths::get_singleton()->get_project_settings_dir().path_join("lib_folding.cfg");
	Error err = config->load(path);
	if (err != OK && err != ERR_FILE_NOT_FOUND) {
		ERR_PRINT("Error loading lib_folding.cfg: " + itos(err));
	}

	// Get unique identifier for this scene+mixer combination.
	const String md = (mixer->get_tree()->get_edited_scene_root()->get_scene_file_path() + String(mixer->get_path())).md5_text();

	Vector<String> collapsed_libs;
	if (config->has_section_key(md, "folding")) {
		collapsed_libs = config->get_value(md, "folding");
	}

	for (int i = collapsed_libs.size() - 1; i >= 0; i--) {
		if (!mixer->has_animation_library(collapsed_libs[i])) {
			collapsed_libs.remove_at(i);
		}
	}

	const String lib_name = p_item->get_text(0);
	if (p_item->is_collapsed()) {
		if (!collapsed_libs.has(lib_name)) {
			collapsed_libs.append(lib_name);
		}
	} else {
		collapsed_libs.erase(lib_name);
	}

	// Plan B recovery mechanism.
	config->set_value(md, "mixer_signature", _get_mixer_signature());

	// Save folding state as text and runtime ID.
	config->set_value(md, "folding", collapsed_libs);

	// Remove deprecated keys.
	if (config->has_section_key(md, "id")) {
		config->erase_section_key(md, "id");
	}
	if (config->has_section_key(md, "root")) {
		config->erase_section_key(md, "root");
	}
	if (config->has_section_key(md, "mixer")) {
		config->erase_section_key(md, "mixer");
	}

	err = config->save(path);
	if (err != OK) {
		ERR_PRINT("Error saving lib_folding.cfg: " + itos(err));
	}
}

Vector<String> AnimationLibraryEditor::_load_mixer_libs_folding() {
	Ref<ConfigFile> config;
	config.instantiate();

	String path = EditorPaths::get_singleton()->get_project_settings_dir().path_join("lib_folding.cfg");
	Error err = config->load(path);
	ERR_FAIL_COND_V_MSG(err != OK && err != ERR_FILE_NOT_FOUND, Vector<String>(), "Error loading lib_folding.cfg: " + itos(err));

	// Get unique identifier for this scene+mixer combination.
	const String md = (mixer->get_tree()->get_edited_scene_root()->get_scene_file_path() + String(mixer->get_path())).md5_text();

	if (!config->has_section(md)) {
		// The scene/mixer combination is no longer valid and we'll try to recover.
		String current_mixer_signature = _get_mixer_signature();

		for (const String &section : config->get_sections()) {
			if (config->get_value(section, "mixer_signature") == current_mixer_signature) {
				config->set_value(md, "mixer_signature", current_mixer_signature);
				config->set_value(md, "folding", config->get_value(section, "folding", PackedStringArray()));

				config->erase_section(section);

				err = config->save(path);
				if (err != OK) {
					ERR_PRINT("Error saving lib_folding.cfg: " + itos(err));
				}
				break;
			}
		}
	}

	return config->get_value(md, "folding", PackedStringArray());
}

String AnimationLibraryEditor::_get_mixer_signature() const {
	String signature = String();

	// Get all libraries sorted for consistency
	LocalVector<StringName> libs;
	mixer->get_animation_library_list(&libs);
	libs.sort_custom<StringName::AlphCompare>();

	// Add libraries and their animations to signature
	for (const StringName &lib_name : libs) {
		signature += "::" + String(lib_name);
		Ref<AnimationLibrary> lib = mixer->get_animation_library(lib_name);
		if (lib.is_valid()) {
			LocalVector<StringName> anims;
			lib->get_animation_list(&anims);
			anims.sort_custom<StringName::AlphCompare>();
			for (const StringName &anim_name : anims) {
				signature += "," + String(anim_name);
			}
		}
	}

	return signature.md5_text();
}

void AnimationLibraryEditor::_set_action_dialog_parent(Node *p_parent) {
	// Standalone quick dialogs must not be children of an invisible Window.
	for (Window *dialog : { static_cast<Window *>(file_dialog), static_cast<Window *>(add_library_dialog), static_cast<Window *>(error_dialog) }) {
		if (dialog->get_parent() != p_parent) {
			dialog->hide();
			dialog->reparent(p_parent);
		}
	}
}

void AnimationLibraryEditor::open_action(int p_action, const StringName &p_library) {
	if (p_action == 0) {
		show_dialog();
		return;
	}
	// Quick actions need only their own dialog, not the full library manager behind it.
	hide();
	_set_action_dialog_parent(get_parent());
	update_tree();
	if (p_action == 1) {
		_add_library();
	} else if (p_action == 2) {
		_load_library();
	} else if (p_action == 3 || p_action == 4) {
		for (TreeItem *item = tree->get_root()->get_first_child(); item; item = item->get_next()) {
			if (StringName(item->get_metadata(0)) == p_library) {
				_button_pressed(item, 0, p_action == 3 ? LIB_BUTTON_ADD : LIB_BUTTON_LOAD, MouseButton::LEFT);
				break;
			}
		}
	}
}

void AnimationLibraryEditor::show_dialog() {
	_set_action_dialog_parent(this);
	update_tree();
	popup_centered_ratio(0.5);
}

void AnimationLibraryEditor::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			new_library_button->set_button_icon(get_editor_theme_icon(SNAME("Add")));
			load_library_button->set_button_icon(get_editor_theme_icon(SNAME("Load")));
		} break;

		case NOTIFICATION_TRANSLATION_CHANGED: {
			tree->set_column_title(0, TTR("Resource"));
			tree->set_column_title(1, TTR("Storage"));
		} break;
	}
}

struct AnimationManagementResourceState {
	Ref<Resource> resource;
	Ref<Resource> data;
	String path;
};
struct AnimationManagementMixerState {
	AnimationMixer *mixer = nullptr;
	HashMap<StringName, Ref<AnimationLibrary>> libraries;
};

bool AnimationLibraryEditor::_commit_management(UndoRedo *p_action) {
	Node *root = get_tree()->get_edited_scene_root();
	if (!root || root->get_scene_file_path().is_empty() || (mixer != root && !root->is_ancestor_of(mixer))) {
		p_action->commit_action(false);
		error_dialog->set_text(TTR("Save the current scene before managing animation resources."));
		add_library_dialog->hide();
		file_dialog->hide();
		error_dialog->popup_centered();
		return false;
	}

	const String scene_path = root->get_scene_file_path();
	HashMap<String, Vector<uint8_t>> files;
	Vector<AnimationManagementResourceState> resources;
	Vector<AnimationManagementMixerState> mixers;
	HashSet<ObjectID> seen;
	auto capture_file = [&](const String &p_path) -> Error {
		if (!p_path.is_resource_file() || !FileAccess::exists(p_path) || files.has(p_path)) {
			return OK;
		}
		Error err;
		Vector<uint8_t> bytes = FileAccess::get_file_as_bytes(p_path, &err);
		if (err == OK) {
			files.insert(p_path, bytes);
		}
		return err;
	};
	auto capture_folder = [&](auto &&self, const String &p_path) -> Error {
		if (!DirAccess::dir_exists_absolute(p_path)) {
			return OK;
		}
		Ref<DirAccess> dir = DirAccess::open(p_path);
		ERR_FAIL_COND_V(dir.is_null(), ERR_CANT_OPEN);
		dir->list_dir_begin();
		for (String name = dir->get_next(); !name.is_empty(); name = dir->get_next()) {
			if (name == "." || name == "..") {
				continue;
			}
			const String path = p_path.path_join(name);
			Error err = dir->current_is_dir() ? self(self, path) : capture_file(path);
			if (err != OK) {
				dir->list_dir_end();
				return err;
			}
		}
		dir->list_dir_end();
		return OK;
	};
	auto capture_resource = [&](const Ref<Resource> &p_resource) -> Error {
		if (seen.has(p_resource->get_instance_id())) {
			return OK;
		}
		seen.insert(p_resource->get_instance_id());
		AnimationManagementResourceState state;
		state.resource = p_resource;
		state.data = p_resource->duplicate();
		state.path = p_resource->get_path();
		resources.push_back(state);
		return capture_file(state.path);
	};
	auto capture_libraries = [&](AnimationMixer *p_mixer) -> Error {
		LocalVector<StringName> names;
		p_mixer->get_animation_library_list(&names);
		for (const StringName &name : names) {
			Ref<AnimationLibrary> library = p_mixer->get_animation_library(name);
			Error err = capture_resource(library);
			if (err != OK) {
				return err;
			}
			if (library->get_path().is_resource_file()) {
				err = capture_folder(capture_folder, library->get_path().get_basename() + ".clips");
				if (err != OK) {
					return err;
				}
			}
			LocalVector<StringName> clips;
			library->get_animation_list(&clips);
			for (const StringName &clip : clips) {
				err = capture_resource(library->get_animation(clip));
				if (err != OK) {
					return err;
				}
			}
		}
		return OK;
	};

	Error err = capture_file(scene_path);
	Ref<Resource> scene_resource = ResourceCache::get_ref(scene_path);
	if (err == OK && scene_resource.is_valid()) {
		err = capture_resource(scene_resource);
	}
	if (err == OK) {
		err = capture_folder(capture_folder, scene_path + ".animations");
	}
	LocalVector<Node *> nodes;
	nodes.push_back(root);
	while (!nodes.is_empty() && err == OK) {
		Node *node = nodes[nodes.size() - 1];
		nodes.remove_at(nodes.size() - 1);
		if (node != root && node->get_owner() != root) {
			continue;
		}
		for (int i = 0; i < node->get_child_count(); i++) {
			nodes.push_back(node->get_child(i));
		}
		AnimationMixer *target = Object::cast_to<AnimationMixer>(node);
		if (target) {
			AnimationManagementMixerState state;
			state.mixer = target;
			LocalVector<StringName> names;
			target->get_animation_library_list(&names);
			for (const StringName &name : names) {
				state.libraries.insert(name, target->get_animation_library(name));
			}
			mixers.push_back(state);
			err = capture_libraries(target);
		}
	}
	if (err != OK) {
		p_action->commit_action(false);
		error_dialog->set_text(TTR("Cannot read existing animation resources. No changes were applied."));
		add_library_dialog->hide();
		file_dialog->hide();
		error_dialog->popup_centered();
		return false;
	}

	p_action->commit_action();
	// Include newly loaded external resources before normalization can change their paths.
	err = capture_libraries(mixer);
	bool scene_references_changed = false;
	for (const AnimationManagementMixerState &state : mixers) {
		LocalVector<StringName> names;
		state.mixer->get_animation_library_list(&names);
		if (names.size() != state.libraries.size()) {
			scene_references_changed = true;
		}
		for (const StringName &name : names) {
			Ref<AnimationLibrary> library = state.mixer->get_animation_library(name);
			if (!state.libraries.has(name) || state.libraries[name] != library || library->is_built_in() || library->get_path().get_extension().to_lower() != "animlib") {
				scene_references_changed = true;
			}
		}
	}
	if (err == OK && scene_references_changed) {
		err = FileAccess::get_read_only_attribute(scene_path) ? ERR_FILE_CANT_WRITE : EditorNode::get_singleton()->save_scene_resources(scene_path, false);
	} else if (err == OK) {
		LocalVector<StringName> names;
		mixer->get_animation_library_list(&names);
		for (const StringName &name : names) {
			Ref<AnimationLibrary> library = mixer->get_animation_library(name);
			err = FileAccess::get_read_only_attribute(library->get_path()) ? ERR_FILE_CANT_WRITE : ResourceSaver::save(library, library->get_path());
			if (err != OK) {
				break;
			}
		}
	}
	// A safe-save rename can fail at close time on some filesystems. Verify the
	// persisted library index as well as the saver result before reporting success.
	if (err == OK) {
		LocalVector<StringName> names;
		mixer->get_animation_library_list(&names);
		for (const StringName &name : names) {
			Ref<AnimationLibrary> library = mixer->get_animation_library(name);
			if (library->is_built_in()) {
				continue;
			}
			Ref<AnimationLibrary> stored = ResourceLoader::load(library->get_path(), "AnimationLibrary", ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP);
			if (stored.is_null() || stored->get_animation_list_size() != library->get_animation_list_size()) {
				err = ERR_FILE_CANT_WRITE;
				break;
			}
			LocalVector<StringName> clips;
			library->get_animation_list(&clips);
			for (const StringName &clip : clips) {
				if (!stored->has_animation(clip) || stored->get_animation(clip)->get_path() != library->get_animation(clip)->get_path()) {
					err = ERR_FILE_CANT_WRITE;
					break;
				}
			}
		}
	}
	if (err == OK && scene_references_changed) {
		Ref<PackedScene> stored_scene = ResourceLoader::load(scene_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP);
		Dictionary stored_libraries;
		bool found = false;
		if (stored_scene.is_valid()) {
			Ref<SceneState> state = stored_scene->get_state();
			const NodePath target_path = root->get_path_to(mixer);
			for (int i = 0; i < state->get_node_count(); i++) {
				if (String(state->get_node_path(i)).trim_prefix("./") != String(target_path).trim_prefix("./")) {
					continue;
				}
				found = true;
				for (int j = 0; j < state->get_node_property_count(i); j++) {
					const String property = state->get_node_property_name(i, j);
					if (property == "libraries") {
						stored_libraries = state->get_node_property_value(i, j);
					} else if (property.begins_with("libraries/")) {
						stored_libraries[StringName(property.trim_prefix("libraries/"))] = state->get_node_property_value(i, j);
					}
				}
				break;
			}
		}
		LocalVector<StringName> names;
		mixer->get_animation_library_list(&names);
		if (!found || stored_libraries.size() != int(names.size())) {
			err = ERR_FILE_CANT_WRITE;
		}
		for (const StringName &name : names) {
			Ref<AnimationLibrary> stored = stored_libraries.get(name, Variant());
			if (stored.is_null() || stored->get_path() != mixer->get_animation_library(name)->get_path()) {
				err = ERR_FILE_CANT_WRITE;
				break;
			}
		}
	}
	if (err != OK) {
		HashSet<String> created_files;
		for (const AnimationManagementResourceState &state : resources) {
			const String path = state.resource->get_path();
			if (path.is_resource_file() && !files.has(path)) {
				created_files.insert(path);
			}
		}
		p_action->undo();
		for (const AnimationManagementResourceState &state : resources) {
			List<StringName> metadata_keys;
			state.resource->get_meta_list(&metadata_keys);
			for (const StringName &name : metadata_keys) {
				if (!state.data->has_meta(name)) {
					state.resource->remove_meta(name);
				}
			}
			state.resource->copy_from(state.data);
			state.resource->set_path(state.path, true);
		}
		for (const AnimationManagementMixerState &state : mixers) {
			LocalVector<StringName> names;
			state.mixer->get_animation_library_list(&names);
			for (const StringName &name : names) {
				state.mixer->remove_animation_library(name);
			}
			for (const KeyValue<StringName, Ref<AnimationLibrary>> &entry : state.libraries) {
				state.mixer->add_animation_library(entry.key, entry.value);
			}
		}
		bool restored = true;
		for (const KeyValue<String, Vector<uint8_t>> &file : files) {
			if (FileAccess::get_file_as_bytes(file.key) == file.value) {
				continue;
			}
			Ref<FileAccess> output = FileAccess::open(file.key, FileAccess::WRITE);
			if (output.is_null() || !output->store_buffer(file.value)) {
				restored = false;
			}
		}
		for (const String &path : created_files) {
			if (FileAccess::exists(path) && DirAccess::remove_absolute(path) != OK) {
				restored = false;
			}
		}
		error_dialog->set_text(restored ? TTR("Animation management could not be saved. The operation was rolled back.") : TTR("Animation management failed and some files could not be restored. Check file permissions."));
		add_library_dialog->hide();
		file_dialog->hide();
		error_dialog->popup_centered();
	}
	p_action->clear_history();
	emit_signal("update_editor", mixer);
	EditorFileSystem::get_singleton()->scan();
	return err == OK;
}

void AnimationLibraryEditor::_update_editor(Object *p_mixer) {
	emit_signal("update_editor", p_mixer);
}

void AnimationLibraryEditor::shortcut_input(const Ref<InputEvent> &p_event) {
	// Resource management is committed immediately; it does not consume the
	// scene's undo history or the workbench's animation-content edit history.
	if (ED_IS_SHORTCUT("ui_undo", p_event) || ED_IS_SHORTCUT("ui_redo", p_event)) {
		set_input_as_handled();
	}
}

void AnimationLibraryEditor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_update_editor", "mixer"), &AnimationLibraryEditor::_update_editor);
	ADD_SIGNAL(MethodInfo("update_editor"));
}

AnimationLibraryEditor::AnimationLibraryEditor() {
	set_title(TTRC("Edit Animation Libraries"));
	set_process_shortcut_input(true);

	file_dialog = memnew(EditorFileDialog);
	add_child(file_dialog);
	file_dialog->connect("file_selected", callable_mp(this, &AnimationLibraryEditor::_load_file));
	file_dialog->connect("files_selected", callable_mp(this, &AnimationLibraryEditor::_load_files));

	add_library_dialog = memnew(ConfirmationDialog);
	add_library_dialog->set_title(TTRC("Library Name:"));

	VBoxContainer *dialog_vb = memnew(VBoxContainer);
	add_library_name = memnew(LineEdit);
	dialog_vb->add_child(add_library_name);
	add_library_name->connect(SceneStringName(text_changed), callable_mp(this, &AnimationLibraryEditor::_add_library_validate));
	add_child(add_library_dialog);

	add_library_validate = memnew(Label);
	dialog_vb->add_child(add_library_validate);
	add_library_dialog->add_child(dialog_vb);
	add_library_dialog->connect(SceneStringName(confirmed), callable_mp(this, &AnimationLibraryEditor::_add_library_confirm));
	add_library_dialog->register_text_enter(add_library_name);

	VBoxContainer *vb = memnew(VBoxContainer);
	HBoxContainer *hb = memnew(HBoxContainer);
	hb->add_spacer(true);
	new_library_button = memnew(Button(TTRC("New Library")));
	new_library_button->set_tooltip_text(TTRC("Create new empty animation library."));
	new_library_button->connect(SceneStringName(pressed), callable_mp(this, &AnimationLibraryEditor::_add_library));
	hb->add_child(new_library_button);
	load_library_button = memnew(Button(TTRC("Load Library")));
	load_library_button->set_tooltip_text(TTRC("Load animation library from disk."));
	load_library_button->connect(SceneStringName(pressed), callable_mp(this, &AnimationLibraryEditor::_load_library));
	hb->add_child(load_library_button);
	vb->add_child(hb);

	MarginContainer *mc = memnew(MarginContainer);
	mc->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	mc->set_theme_type_variation("NoBorderHorizontalWindow");
	vb->add_child(mc);

	tree = memnew(Tree);
	tree->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	tree->set_theme_type_variation("TreeTable");
	tree->set_columns(2);
	tree->set_column_titles_visible(true);
	tree->set_column_expand(0, true);
	tree->set_column_custom_minimum_width(1, EDSCALE * 250);
	tree->set_column_expand(1, false);
	tree->set_hide_root(true);
	tree->set_hide_folding(false);
	tree->set_scroll_hint_mode(Tree::SCROLL_HINT_MODE_BOTTOM);
	mc->add_child(tree);
	tree->connect("item_edited", callable_mp(this, &AnimationLibraryEditor::_item_renamed));
	tree->connect("button_clicked", callable_mp(this, &AnimationLibraryEditor::_button_pressed));
	tree->connect("item_collapsed", callable_mp(this, &AnimationLibraryEditor::_save_mixer_lib_folding));

	file_popup = memnew(PopupMenu);
	add_child(file_popup);
	file_popup->connect(SceneStringName(id_pressed), callable_mp(this, &AnimationLibraryEditor::_file_popup_selected));

	add_child(vb);

	error_dialog = memnew(AcceptDialog);
	error_dialog->set_flag(Window::FLAG_RESIZE_DISABLED, true);
	error_dialog->set_title(TTRC("Error:"));
	add_child(error_dialog);
}
