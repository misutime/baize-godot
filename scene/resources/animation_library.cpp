/**************************************************************************/
/*  animation_library.cpp                                                 */
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

#include "animation_library.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/node_3d.h"
#include "scene/animation/animation_player.h"
#include "scene/scene_string_names.h"

bool AnimationLibrary::is_valid_animation_name(const String &p_name) {
	return !(p_name.is_empty() || p_name.contains_char('/') || p_name.contains_char(':') || p_name.contains_char(',') || p_name.contains_char('['));
}

bool AnimationLibrary::is_valid_library_name(const String &p_name) {
	return !(p_name.contains_char('/') || p_name.contains_char(':') || p_name.contains_char(',') || p_name.contains_char('['));
}

String AnimationLibrary::validate_library_name(const String &p_name) {
	return p_name.replace_chars("/:,[", '_');
}

Error AnimationLibrary::add_animation(const StringName &p_name, const Ref<Animation> &p_animation) {
	ERR_FAIL_COND_V_MSG(!is_valid_animation_name(p_name), ERR_INVALID_PARAMETER, "Invalid animation name: '" + String(p_name) + "'.");
	ERR_FAIL_COND_V(p_animation.is_null(), ERR_INVALID_PARAMETER);

	if (animations.has(p_name)) {
		animations[p_name]->disconnect_changed(callable_mp(this, &AnimationLibrary::_animation_changed));
		animations.erase(p_name);
		emit_signal(SNAME("animation_removed"), p_name);
	}

	animations.insert(p_name, p_animation);
	animations[p_name]->connect_changed(callable_mp(this, &AnimationLibrary::_animation_changed).bind(p_name));
	emit_signal(SNAME("animation_added"), p_name);
	notify_property_list_changed();
	return OK;
}

void AnimationLibrary::remove_animation(const StringName &p_name) {
	ERR_FAIL_COND_MSG(!animations.has(p_name), vformat("Animation not found: %s.", p_name));

	animations[p_name]->disconnect_changed(callable_mp(this, &AnimationLibrary::_animation_changed));
	animations.erase(p_name);
	emit_signal(SNAME("animation_removed"), p_name);
	notify_property_list_changed();
}

void AnimationLibrary::rename_animation(const StringName &p_name, const StringName &p_new_name) {
	ERR_FAIL_COND_MSG(!animations.has(p_name), vformat("Animation not found: %s.", p_name));
	ERR_FAIL_COND_MSG(!is_valid_animation_name(p_new_name), "Invalid animation name: '" + String(p_new_name) + "'.");
	ERR_FAIL_COND_MSG(animations.has(p_new_name), vformat("Animation name \"%s\" already exists in library.", p_new_name));

	animations[p_name]->disconnect_changed(callable_mp(this, &AnimationLibrary::_animation_changed));
	animations[p_name]->connect_changed(callable_mp(this, &AnimationLibrary::_animation_changed).bind(p_new_name));
	animations.insert(p_new_name, animations[p_name]);
	animations.erase(p_name);
	emit_signal(SNAME("animation_renamed"), p_name, p_new_name);
}

bool AnimationLibrary::has_animation(const StringName &p_name) const {
	return animations.has(p_name);
}

Ref<Animation> AnimationLibrary::get_animation(const StringName &p_name) const {
	ERR_FAIL_COND_V_MSG(!animations.has(p_name), Ref<Animation>(), vformat("Animation not found: \"%s\".", p_name));

	return animations[p_name];
}

TypedArray<StringName> AnimationLibrary::_get_animation_list() const {
	TypedArray<StringName> ret;
	LocalVector<StringName> names;
	get_animation_list(&names);
	for (const StringName &K : names) {
		ret.push_back(K);
	}
	return ret;
}

void AnimationLibrary::_animation_changed(const StringName &p_name) {
	emit_signal(SceneStringName(animation_changed), p_name);
}

void AnimationLibrary::get_animation_list(LocalVector<StringName> *p_animations) const {
	LocalVector<StringName> anims;

	for (const KeyValue<StringName, Ref<Animation>> &E : animations) {
		anims.push_back(E.key);
	}

	anims.sort_custom<StringName::AlphCompare>();

	for (const StringName &E : anims) {
		p_animations->push_back(E);
	}
}

int AnimationLibrary::get_animation_list_size() const {
	return animations.size();
}

void AnimationLibrary::_set_data(const Dictionary &p_data) {
	for (KeyValue<StringName, Ref<Animation>> &K : animations) {
		K.value->disconnect_changed(callable_mp(this, &AnimationLibrary::_animation_changed));
	}
	animations.clear();
	for (const KeyValue<Variant, Variant> &kv : p_data) {
		add_animation(kv.key, kv.value);
	}
}

Dictionary AnimationLibrary::_get_data() const {
	Dictionary ret;
	for (const KeyValue<StringName, Ref<Animation>> &K : animations) {
		ret[K.key] = K.value;
	}
	return ret;
}

#ifdef TOOLS_ENABLED
void AnimationLibrary::get_argument_options(const StringName &p_function, int p_idx, List<String> *r_options) const {
	const String pf = p_function;
	if (p_idx == 0 && (pf == "get_animation" || pf == "has_animation" || pf == "rename_animation" || pf == "remove_animation")) {
		LocalVector<StringName> names;
		get_animation_list(&names);
		for (const StringName &E : names) {
			r_options->push_back(E.string().quote());
		}
	}
	Resource::get_argument_options(p_function, p_idx, r_options);
}
#endif

void AnimationLibrary::_bind_methods() {
	ClassDB::bind_method(D_METHOD("add_animation", "name", "animation"), &AnimationLibrary::add_animation);
	ClassDB::bind_method(D_METHOD("remove_animation", "name"), &AnimationLibrary::remove_animation);
	ClassDB::bind_method(D_METHOD("rename_animation", "name", "newname"), &AnimationLibrary::rename_animation);
	ClassDB::bind_method(D_METHOD("has_animation", "name"), &AnimationLibrary::has_animation);
	ClassDB::bind_method(D_METHOD("get_animation", "name"), &AnimationLibrary::get_animation);
	ClassDB::bind_method(D_METHOD("get_animation_list"), &AnimationLibrary::_get_animation_list);
	ClassDB::bind_method(D_METHOD("get_animation_list_size"), &AnimationLibrary::get_animation_list_size);

	ClassDB::bind_method(D_METHOD("_set_data", "data"), &AnimationLibrary::_set_data);
	ClassDB::bind_method(D_METHOD("_get_data"), &AnimationLibrary::_get_data);

	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "_data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL), "_set_data", "_get_data");

	ADD_SIGNAL(MethodInfo("animation_added", PropertyInfo(Variant::STRING_NAME, "anim_name")));
	ADD_SIGNAL(MethodInfo("animation_removed", PropertyInfo(Variant::STRING_NAME, "anim_name")));
	ADD_SIGNAL(MethodInfo("animation_renamed", PropertyInfo(Variant::STRING_NAME, "old_name"), PropertyInfo(Variant::STRING_NAME, "new_name")));
	ADD_SIGNAL(MethodInfo("animation_changed", PropertyInfo(Variant::STRING_NAME, "anim_name")));
}
AnimationLibrary::AnimationLibrary() {
}

// A text animation library is an index of standalone clips, never a container for key data.
Error AnimationLibrary::save_external_animations(const String &p_library_path) {
	const String folder = p_library_path.get_basename() + ".clips";
	LocalVector<StringName> names;
	get_animation_list(&names);
	for (const StringName &name : names) {
		Ref<Animation> animation = get_animation(name);
		const String old_path = animation->get_path();
		if (old_path.is_resource_file() && !animation->is_built_in() && old_path.get_extension().to_lower() == "anim") {
			continue;
		}
		Error err = DirAccess::make_dir_recursive_absolute(folder);
		ERR_FAIL_COND_V(err != OK, err);
		const String key = name;
		const String path = folder.path_join(key.validate_filename() + "-" + key.md5_text().substr(0, 8) + ".anim");
		if (FileAccess::exists(path)) {
			Ref<Animation> existing = ResourceLoader::load(path, "Animation", ResourceFormatLoader::CACHE_MODE_IGNORE);
			ERR_FAIL_COND_V_MSG(existing.is_null() || String(existing->get_meta("_animlib_owner", "")) != p_library_path || String(existing->get_meta("_animlib_key", "")) != key, ERR_ALREADY_EXISTS, "Animation output belongs to another resource: " + path);
		}
		animation->set_meta("_animlib_owner", p_library_path);
		animation->set_meta("_animlib_key", key);
		err = ResourceSaver::save(animation, path);
		ERR_FAIL_COND_V(err != OK, err);
		Ref<Animation> cached = ResourceCache::get_ref(path);
		if (cached.is_valid() && cached != animation) {
			cached->copy_from(animation);
			animation = cached;
			add_animation(name, animation);
		}
		animation->set_path(path, true);
	}
	return OK;
}

// Export all 3D animation libraries before packing the scene. Paths depend on source
// and player/library identity, so reimports overwrite the same files and keep UIDs.
Error AnimationLibrary::save_scene_libraries(Node *p_scene, const String &p_source, bool p_import, double p_sample_rate) {
	LocalVector<Node *> pending;
	pending.push_back(p_scene);
	while (!pending.is_empty()) {
		Node *node = pending[pending.size() - 1];
		pending.remove_at(pending.size() - 1);
		if (!p_import && node != p_scene && node->get_owner() != p_scene) {
			continue; // Skip preview nodes and unedited children of other scene instances.
		}
		for (int i = 0; i < node->get_child_count(); i++) {
			pending.push_back(node->get_child(i));
		}
		AnimationPlayer *player = Object::cast_to<AnimationPlayer>(node);
		if (!player || !Object::cast_to<Node3D>(player->get_node_or_null(player->get_root_node()))) {
			continue;
		}
		LocalVector<StringName> names;
		player->get_animation_library_list(&names);
		for (const StringName &name : names) {
			Ref<AnimationLibrary> library = player->get_animation_library(name);
			if (p_import && p_sample_rate > 0.0) {
				LocalVector<StringName> clip_names;
				library->get_animation_list(&clip_names);
				for (const StringName &clip_name : clip_names) {
					Ref<Animation> clip = library->get_animation(clip_name);
					clip->set_meta("_animation_sample_rate", p_sample_rate);
					// Advanced import settings may already have written an external clip.
					if (!clip->is_built_in() && clip->get_path().get_extension().to_lower() == "anim") {
						Error err = ResourceSaver::save(clip, clip->get_path());
						ERR_FAIL_COND_V(err != OK, err);
					}
				}
			}
			if (!p_import && library->get_path().is_resource_file() && library->get_path().get_extension().to_lower() == "animlib") {
				Error err = ResourceSaver::save(library, library->get_path());
				ERR_FAIL_COND_V(err != OK, err);
				continue;
			}
			const String identity = String(p_scene->get_path_to(player)) + "/" + String(name);
			const String folder = p_source + ".animations";
			Error err = DirAccess::make_dir_recursive_absolute(folder);
			ERR_FAIL_COND_V(err != OK, err);
			const String path = folder.path_join(identity.validate_filename() + "-" + identity.md5_text().substr(0, 8) + ".animlib");
			if (FileAccess::exists(path)) {
				Ref<AnimationLibrary> previous = ResourceLoader::load(path, "AnimationLibrary", ResourceFormatLoader::CACHE_MODE_IGNORE);
				ERR_FAIL_COND_V_MSG(previous.is_null() || String(previous->get_meta("_animation_source", "")) != p_source, ERR_ALREADY_EXISTS, "Animation library output belongs to another resource: " + path);
			}
			library->set_meta("_animation_source", p_source);
			err = ResourceSaver::save(library, path);
			ERR_FAIL_COND_V(err != OK, err);
			Ref<AnimationLibrary> cached = ResourceCache::get_ref(path);
			if (cached.is_valid() && cached != library) {
				cached->copy_from(library);
				library = cached;
				player->remove_animation_library(name);
				player->add_animation_library(name, library);
			}
			library->set_path(path, true);
		}
	}
	return OK;
}
