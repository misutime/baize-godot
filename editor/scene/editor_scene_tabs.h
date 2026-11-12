/**************************************************************************/
/*  editor_scene_tabs.h                                                   */
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

#include "scene/gui/margin_container.h"

class Button;
class ConfigFile;
class HBoxContainer;
class MenuButton;
class Panel;
class PanelContainer;
class PopupMenu;
class TabBar;
class TextureRect;

class EditorSceneTabs : public MarginContainer {
	GDCLASS(EditorSceneTabs, MarginContainer);

	inline static EditorSceneTabs *singleton = nullptr;

public:
	enum {
		SCENE_SAVE_SCENE = 1000, // Prevents conflicts with EditorNode options.
		SCENE_SAVE_AS_SCENE,
		SCENE_SHOW_IN_FILESYSTEM,
		SCENE_RUN,
		SCENE_SET_AS_MAIN_SCENE,
		SCENE_CLOSE,
		SCENE_CLOSE_OTHERS,
		SCENE_CLOSE_RIGHT,
		DOCUMENT_TOGGLE_PIN,
	};

private:
	PanelContainer *tabbar_panel = nullptr;
	HBoxContainer *tabbar_container = nullptr;

	TabBar *scene_tabs = nullptr;
	PopupMenu *scene_tabs_context_menu = nullptr;
	MenuButton *scene_list = nullptr;
	Button *scene_tab_add = nullptr;
	Control *scene_tab_add_ph = nullptr;

	Panel *tab_preview_panel = nullptr;
	TextureRect *tab_preview = nullptr;

	String context_tab;
	String tab_to_keep_after_close;
	Vector<String> tab_order;
	HashSet<String> pinned_tabs;
	bool restoring_documents = false;
	int _scene_index(int p_tab) const;
	int _scene_tab(int p_scene) const;
	int _current_tab() const;
	int _context_tab() const;
	String _scene_key(int p_scene) const;
	void _sync_tab_order();
	void _save_layout_delayed();
	struct ResourceDocument {
		String path;
		String title;
		Ref<Texture2D> icon;
		Callable activate;
		Callable save;
		Callable save_as;
		Callable close;
		Callable navigate;
		bool modified = false;
	};
	Vector<ResourceDocument> documents;
	String active_document;
	int _find_document(const String &p_path) const;
	int _document_index(int p_tab) const;
	void _activate_document(const String &p_path);
	void _activate_scene(int p_scene);
	void _request_navigation(const Callable &p_action);
	void _remove_document(const String &p_path);
	void _request_document_close(const String &p_path, const Callable &p_action);
	void _menu_option(int p_option);
	void _run_scene_action(const Callable &p_action);
	void _close_documents(const PackedStringArray &p_paths, const Callable &p_after_close);
	void _close_document_and_continue(const String &p_path, const PackedStringArray &p_remaining, const Callable &p_after_close);
	void _close_scene_documents(int p_option);

	void _update_visibility();
	void _scene_tab_changed(int p_tab);
	void _scene_tab_script_edited(int p_tab);
	void _scene_tab_closed(int p_tab);
	void _close_scene_tab(int p_scene);
	void _scene_tab_hovered(int p_tab);
	void _scene_tab_exit();
	void _scene_tab_input(const Ref<InputEvent> &p_input);
	void _scene_tabs_resized();

	void _update_tab_titles();
	void _reposition_active_tab(int p_to_index);
	void _update_context_menu(int p_index);
	void _custom_menu_option(int p_option);
	void _update_scene_list();

	void _tab_preview_done(const String &p_path, const Ref<Texture2D> &p_preview, const Ref<Texture2D> &p_small_preview, int p_tab);
	static Dictionary _get_context_data(int p_current_tab);

	void _global_menu_scene(const Variant &p_tag);
	void _global_menu_new_window(const Variant &p_tag);

	void _project_settings_changed();

	virtual void shortcut_input(const Ref<InputEvent> &p_event) override;

protected:
	void _notification(int p_what);
	virtual void unhandled_key_input(const Ref<InputEvent> &p_event) override;
	static void _bind_methods();

public:
	static EditorSceneTabs *get_singleton() { return singleton; }

	void add_extra_button(Button *p_button);

	void set_current_tab(int p_tab);
	int get_current_tab() const;
	int get_option_tab() const;

	void update_scene_tabs();
	void open_document(const String &p_path, const String &p_title, const Ref<Texture2D> &p_icon, const Callable &p_activate, const Callable &p_save, const Callable &p_close, const Callable &p_navigate, const Callable &p_save_as, bool p_activate_document);
	void remove_document(const String &p_path);
	void set_document_modified(const String &p_path, bool p_modified);
	String get_active_document() const { return active_document; }
	bool handle_document_menu(int p_option);
	bool request_scene_action(const Callable &p_action);
	bool has_modified_documents() const;
	bool can_save_document_as() const;
	void save_documents();
	Vector<int> get_scene_tabs_to_close(int p_option) const;
	void finish_closing_tabs();
	void cancel_closing_tabs() { tab_to_keep_after_close = String(); }
	void save_layout_to_config(const Ref<ConfigFile> &p_config);
	void restore_layout_from_config(const Ref<ConfigFile> &p_config);
	void set_tab_pinned(int p_tab, bool p_pinned);
	bool is_tab_pinned(int p_tab) const;

	EditorSceneTabs();
};
