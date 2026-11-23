/**************************************************************************/
/*  skeleton_definition.cpp                                               */
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

#include "skeleton_definition.h"

#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/io/resource_uid.h"
#include "core/object/class_db.h"
#include "scene/resources/animation.h"
#include "scene/resources/animation_library.h"

Error SkeletonDefinition::set_bones(const TypedArray<Dictionary> &p_bones) {
	Vector<Bone> new_bones;
	HashMap<StringName, int> new_indices;
	new_bones.resize(p_bones.size());
	for (int i = 0; i < p_bones.size(); i++) {
		const Dictionary entry = p_bones[i];
		const Variant bone_name = entry.get("name", Variant());
		const Variant parent = entry.get("parent", Variant());
		const Variant rest = entry.get("rest", Variant());
		if ((bone_name.get_type() != Variant::STRING && bone_name.get_type() != Variant::STRING_NAME) || parent.get_type() != Variant::INT || rest.get_type() != Variant::TRANSFORM3D) {
			return ERR_INVALID_DATA;
		}
		Bone &bone = new_bones.write[i];
		bone.name = bone_name;
		const String bone_name_string = bone.name;
		const int64_t parent_index = parent;
		bone.rest = rest;
		if (bone.name == StringName() || bone_name_string.contains(":") || bone_name_string.contains("/") || new_indices.has(bone.name) || parent_index < -1 || parent_index >= p_bones.size() || parent_index == i || !bone.rest.is_finite()) {
			return ERR_INVALID_DATA;
		}
		bone.parent = parent_index;
		new_indices.insert(bone.name, i);
	}

	// Bone indices need not be in parent-first order. Reject cycles before replacing any data.
	Vector<uint8_t> visited;
	visited.resize(new_bones.size());
	visited.fill(0);
	for (int i = 0; i < new_bones.size(); i++) {
		int current = i;
		while (current != -1 && visited[current] == 0) {
			visited.write[current] = 1;
			current = new_bones[current].parent;
		}
		if (current != -1 && visited[current] == 1) {
			return ERR_INVALID_DATA;
		}
		current = i;
		while (current != -1 && visited[current] == 1) {
			visited.write[current] = 2;
			current = new_bones[current].parent;
		}
	}

	bones = new_bones;
	bone_indices = new_indices;
	emit_changed();
	return OK;
}

void SkeletonDefinition::_set_bones(const TypedArray<Dictionary> &p_bones) {
	ERR_FAIL_COND_MSG(set_bones(p_bones) != OK, "Invalid skeleton definition. Bones require unique names, valid parents, finite rest transforms, and an acyclic hierarchy.");
}

TypedArray<Dictionary> SkeletonDefinition::get_bones() const {
	TypedArray<Dictionary> result;
	for (const Bone &bone : bones) {
		Dictionary entry;
		entry["name"] = bone.name;
		entry["parent"] = bone.parent;
		entry["rest"] = bone.rest;
		result.push_back(entry);
	}
	return result;
}

int SkeletonDefinition::get_bone_count() const {
	return bones.size();
}

int SkeletonDefinition::find_bone(const StringName &p_name) const {
	const int *index = bone_indices.getptr(p_name);
	return index ? *index : -1;
}

StringName SkeletonDefinition::get_bone_name(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, bones.size(), StringName());
	return bones[p_index].name;
}

int SkeletonDefinition::get_bone_parent(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, bones.size(), -1);
	return bones[p_index].parent;
}

Transform3D SkeletonDefinition::get_bone_rest(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, bones.size(), Transform3D());
	return bones[p_index].rest;
}

Dictionary SkeletonDefinition::compare(const Ref<SkeletonDefinition> &p_other, real_t p_tolerance) const {
	ERR_FAIL_COND_V(p_other.is_null(), Dictionary());
	ERR_FAIL_COND_V(!Math::is_finite(p_tolerance) || p_tolerance < 0.0, Dictionary());
	PackedStringArray missing_bones;
	PackedStringArray extra_bones;
	PackedStringArray parent_mismatches;
	PackedStringArray rest_mismatches;
	for (const Bone &bone : bones) {
		const int other_index = p_other->find_bone(bone.name);
		if (other_index == -1) {
			missing_bones.push_back(bone.name);
			continue;
		}
		const Bone &other = p_other->bones[other_index];
		const StringName parent_name = bone.parent == -1 ? StringName() : bones[bone.parent].name;
		const StringName other_parent_name = other.parent == -1 ? StringName() : p_other->bones[other.parent].name;
		if (parent_name != other_parent_name) {
			parent_mismatches.push_back(bone.name);
		}
		bool rest_matches = true;
		for (int axis = 0; axis < 3; axis++) {
			if (Math::abs(bone.rest.origin[axis] - other.rest.origin[axis]) > p_tolerance) {
				rest_matches = false;
			}
			for (int component = 0; component < 3; component++) {
				if (Math::abs(bone.rest.basis[axis][component] - other.rest.basis[axis][component]) > p_tolerance) {
					rest_matches = false;
				}
			}
		}
		if (!rest_matches) {
			rest_mismatches.push_back(bone.name);
		}
	}
	for (const Bone &bone : p_other->bones) {
		if (!bone_indices.has(bone.name)) {
			extra_bones.push_back(bone.name);
		}
	}
	const bool hierarchy_matches = !bones.is_empty() && !p_other->bones.is_empty() && missing_bones.is_empty() && extra_bones.is_empty() && parent_mismatches.is_empty();
	Dictionary result;
	result["compatible"] = hierarchy_matches && rest_mismatches.is_empty();
	result["hierarchy_matches"] = hierarchy_matches;
	// Tracks store absolute local poses, so clips stay usable when rests differ or either side has extra bones.
	const int common_bones = bones.size() - missing_bones.size();
	result["common_bones"] = common_bones;
	result["usable"] = common_bones > 0 && parent_mismatches.is_empty() && common_bones * 2 >= MIN(bones.size(), p_other->bones.size());
	result["missing_bones"] = missing_bones;
	result["extra_bones"] = extra_bones;
	result["parent_mismatches"] = parent_mismatches;
	result["rest_mismatches"] = rest_mismatches;
	return result;
}

SkeletonDefinition::~SkeletonDefinition() {}

void SkeletonDefinition::set_animation_directory(const String &p_directory) {
	const String directory = p_directory.strip_edges().trim_suffix("/");
	if (animation_directory == directory) {
		return;
	}
	animation_directory = directory;
	scanned = false;
	emit_changed();
}

String SkeletonDefinition::get_animation_directory() const {
	return animation_directory;
}

String SkeletonDefinition::get_animation_directory_path() const {
	const String path = get_path();
	if (path.is_empty() || path.contains("::")) {
		return String();
	}
	const String directory = animation_directory.is_empty() ? path.get_file().get_basename() + "_anims" : animation_directory;
	if (directory.begins_with("res://")) {
		return directory;
	}
	return path.get_base_dir().path_join(directory).simplify_path();
}

void SkeletonDefinition::set_baked_animation_libraries(const Dictionary &p_libraries) {
	baked_libraries.clear();
	for (const KeyValue<Variant, Variant> &entry : p_libraries) {
		Ref<AnimationLibrary> library = entry.value;
		if (library.is_valid()) {
			baked_libraries[StringName(entry.key)] = library;
		}
	}
	emit_changed();
}

Dictionary SkeletonDefinition::get_baked_animation_libraries() const {
	return baked_libraries;
}

bool SkeletonDefinition::has_baked_animation_libraries() const {
	return !baked_libraries.is_empty();
}

bool SkeletonDefinition::_set(const StringName &p_name, const Variant &p_value) {
	// Files written before the animation directory existed list their clips in one library.
	if (p_name == SNAME("animation_library")) {
		Ref<AnimationLibrary> library = p_value;
		if (library.is_valid()) {
			baked_libraries[StringName()] = library;
		}
		return true;
	}
	if (p_name == SNAME("animation_metadata")) {
		return true;
	}
	return false;
}

void SkeletonDefinition::_add_scan_issue(const String &p_path, const String &p_reason, const String &p_message) const {
	Dictionary issue;
	issue["path"] = p_path;
	issue["reason"] = p_reason;
	issue["message"] = p_message;
	scan_issues.push_back(issue);
}

void SkeletonDefinition::_scan_directory(const String &p_directory, HashMap<StringName, Ref<Animation>> &r_clips) const {
	for (const String &file : ResourceLoader::list_directory(p_directory)) {
		if (file.ends_with("/") || file.get_extension().to_lower() != "skanim") {
			continue;
		}
		const String path = p_directory.path_join(file);
		const String animation_name = file.get_basename();
		if (!AnimationLibrary::is_valid_animation_name(animation_name)) {
			_add_scan_issue(path, "invalid_name", vformat(RTR("The file name '%s' cannot be used as an animation name. Remove /, :, comma and brackets."), animation_name));
			continue;
		}
		Ref<Animation> clip = ResourceLoader::load(path, "Animation");
		if (clip.is_null()) {
			_add_scan_issue(path, "load_failed", RTR("The clip file could not be loaded."));
			continue;
		}
		// The clip's own skeleton reference guards against clips copied in from another skeleton.
		if (clip->get_skeleton_path() != get_path()) {
			_add_scan_issue(path, "foreign_skeleton", vformat(RTR("The clip belongs to skeleton '%s'."), clip->get_skeleton_path()));
			continue;
		}
		r_clips.insert(animation_name, clip);
	}
}

bool SkeletonDefinition::_scan(bool p_force) const {
	const String root = get_animation_directory_path();
	if (scanned && !p_force && scanned_root == root) {
		return false;
	}
	scanned = true;
	scanned_root = root;
	scan_issues.clear();

	HashMap<StringName, HashMap<StringName, Ref<Animation>>> found;
	{
		// The default library always exists, also for a skeleton without clips.
		HashMap<StringName, Ref<Animation>> clips;
		if (!root.is_empty()) {
			_scan_directory(root, clips);
		}
		found.insert(StringName(), clips);
	}
	if (!root.is_empty()) {
		for (const String &entry : ResourceLoader::list_directory(root)) {
			if (!entry.ends_with("/")) {
				continue;
			}
			const String library_name = entry.trim_suffix("/");
			if (library_name.begins_with(".")) {
				continue;
			}
			const String library_path = root.path_join(library_name);
			if (!AnimationLibrary::is_valid_library_name(library_name)) {
				_add_scan_issue(library_path, "invalid_name", vformat(RTR("The directory name '%s' cannot be used as an animation library name."), library_name));
				continue;
			}
			HashMap<StringName, Ref<Animation>> clips;
			_scan_directory(library_path, clips);
			for (const String &nested : ResourceLoader::list_directory(library_path)) {
				if (nested.ends_with("/") && !nested.begins_with(".")) {
					_add_scan_issue(library_path.path_join(nested.trim_suffix("/")), "nested_directory", RTR("Only one directory level below the animation directory becomes an animation library."));
				}
			}
			found.insert(library_name, clips);
		}
	}

	// Update the existing library objects in place: their signals keep every player in sync.
	bool changed = false;
	Vector<StringName> stale_libraries;
	for (const KeyValue<StringName, Ref<AnimationLibrary>> &entry : scanned_libraries) {
		if (!found.has(entry.key)) {
			stale_libraries.push_back(entry.key);
		}
	}
	for (const StringName &library_name : stale_libraries) {
		scanned_libraries.erase(library_name);
		changed = true;
	}
	for (const KeyValue<StringName, HashMap<StringName, Ref<Animation>>> &entry : found) {
		Ref<AnimationLibrary> library;
		if (scanned_libraries.has(entry.key)) {
			library = scanned_libraries[entry.key];
		} else {
			library.instantiate();
			scanned_libraries.insert(entry.key, library);
			changed = true;
		}
		LocalVector<StringName> existing;
		library->get_animation_list(&existing);
		for (const StringName &animation_name : existing) {
			const Ref<Animation> *clip = entry.value.getptr(animation_name);
			if (!clip || *clip != library->get_animation(animation_name)) {
				library->remove_animation(animation_name);
				changed = true;
			}
		}
		for (const KeyValue<StringName, Ref<Animation>> &clip : entry.value) {
			if (!library->has_animation(clip.key)) {
				library->add_animation(clip.key, clip.value);
				changed = true;
			}
		}
	}
	return changed;
}

bool SkeletonDefinition::rescan_animations() {
	if (has_baked_animation_libraries()) {
		return false;
	}
	const bool changed = _scan(true);
	if (changed) {
		emit_changed();
		emit_signal(SNAME("animations_changed"));
	}
	return changed;
}

TypedArray<Dictionary> SkeletonDefinition::get_animation_scan_issues() const {
	if (!has_baked_animation_libraries()) {
		_scan(false);
	}
	return scan_issues.duplicate();
}

Ref<AnimationLibrary> SkeletonDefinition::get_animation_library(const StringName &p_library) const {
	if (has_baked_animation_libraries()) {
		if (baked_libraries.has(p_library)) {
			return baked_libraries[p_library];
		}
	} else {
		_scan(false);
		const Ref<AnimationLibrary> *library = scanned_libraries.getptr(p_library);
		if (library) {
			return *library;
		}
	}
	if (p_library != StringName()) {
		return Ref<AnimationLibrary>();
	}
	if (fallback_library.is_null()) {
		fallback_library.instantiate();
	}
	return fallback_library;
}

void SkeletonDefinition::get_animation_library_list(LocalVector<StringName> *p_libraries) const {
	if (has_baked_animation_libraries()) {
		for (const KeyValue<Variant, Variant> &entry : baked_libraries) {
			p_libraries->push_back(StringName(entry.key));
		}
		if (!baked_libraries.has(StringName())) {
			p_libraries->push_back(StringName());
		}
	} else {
		_scan(false);
		for (const KeyValue<StringName, Ref<AnimationLibrary>> &entry : scanned_libraries) {
			p_libraries->push_back(entry.key);
		}
	}
	p_libraries->sort_custom<StringName::AlphCompare>();
}

TypedArray<StringName> SkeletonDefinition::get_animation_library_names() const {
	LocalVector<StringName> libraries;
	get_animation_library_list(&libraries);
	TypedArray<StringName> names;
	for (const StringName &library_name : libraries) {
		names.push_back(library_name);
	}
	return names;
}

Dictionary SkeletonDefinition::bake_animation_libraries() const {
	Dictionary baked;
	LocalVector<StringName> libraries;
	get_animation_library_list(&libraries);
	for (const StringName &library_name : libraries) {
		Ref<AnimationLibrary> source = get_animation_library(library_name);
		// Copies keep the live libraries free of the paths a saver assigns to sub-resources.
		Ref<AnimationLibrary> copy;
		copy.instantiate();
		LocalVector<StringName> names;
		source->get_animation_list(&names);
		for (const StringName &animation_name : names) {
			copy->add_animation(animation_name, source->get_animation(animation_name));
		}
		baked[library_name] = copy;
	}
	return baked;
}

TypedArray<Dictionary> SkeletonDefinition::get_animation_catalog() const {
	TypedArray<Dictionary> catalog;
	LocalVector<StringName> libraries;
	get_animation_library_list(&libraries);
	for (const StringName &library_name : libraries) {
		Ref<AnimationLibrary> library = get_animation_library(library_name);
		LocalVector<StringName> names;
		library->get_animation_list(&names);
		for (const StringName &animation_name : names) {
			Ref<Animation> clip = library->get_animation(animation_name);
			const String path = clip->get_path();
			Dictionary entry;
			entry["name"] = library_name == StringName() ? animation_name : StringName(String(library_name) + "/" + String(animation_name));
			entry["library"] = library_name;
			entry["animation"] = animation_name;
			entry["path"] = path;
			entry["uid"] = path.is_empty() ? ResourceUID::INVALID_ID : ResourceLoader::get_resource_uid(path);
			catalog.push_back(entry);
		}
	}
	return catalog;
}

Error SkeletonDefinition::prepare_for_save(const String &p_path) {
	// Store the directory explicitly, so renaming the skeleton file later keeps its clips.
	if (animation_directory.is_empty() && !p_path.is_empty()) {
		animation_directory = p_path.get_file().get_basename() + "_anims";
	}
	return OK;
}

void SkeletonDefinition::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_animation_directory", "directory"), &SkeletonDefinition::set_animation_directory);
	ClassDB::bind_method(D_METHOD("get_animation_directory"), &SkeletonDefinition::get_animation_directory);
	ClassDB::bind_method(D_METHOD("get_animation_directory_path"), &SkeletonDefinition::get_animation_directory_path);
	ClassDB::bind_method(D_METHOD("set_baked_animation_libraries", "libraries"), &SkeletonDefinition::set_baked_animation_libraries);
	ClassDB::bind_method(D_METHOD("get_baked_animation_libraries"), &SkeletonDefinition::get_baked_animation_libraries);
	ClassDB::bind_method(D_METHOD("has_baked_animation_libraries"), &SkeletonDefinition::has_baked_animation_libraries);
	ClassDB::bind_method(D_METHOD("bake_animation_libraries"), &SkeletonDefinition::bake_animation_libraries);
	ClassDB::bind_method(D_METHOD("get_animation_library", "library"), &SkeletonDefinition::get_animation_library, DEFVAL(StringName()));
	ClassDB::bind_method(D_METHOD("get_animation_library_names"), &SkeletonDefinition::get_animation_library_names);
	ClassDB::bind_method(D_METHOD("rescan_animations"), &SkeletonDefinition::rescan_animations);
	ClassDB::bind_method(D_METHOD("get_animation_scan_issues"), &SkeletonDefinition::get_animation_scan_issues);
	ClassDB::bind_method(D_METHOD("get_animation_catalog"), &SkeletonDefinition::get_animation_catalog);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "animation_directory"), "set_animation_directory", "get_animation_directory");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "animation_libraries", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_baked_animation_libraries", "get_baked_animation_libraries");
	ADD_SIGNAL(MethodInfo("animations_changed"));
	ClassDB::bind_method(D_METHOD("set_preview_mesh", "path"), &SkeletonDefinition::set_preview_mesh);
	ClassDB::bind_method(D_METHOD("get_preview_mesh"), &SkeletonDefinition::get_preview_mesh);
	ClassDB::bind_method(D_METHOD("_set_preview_mesh_uid", "uid"), &SkeletonDefinition::_set_preview_mesh_uid);
	ClassDB::bind_method(D_METHOD("_get_preview_mesh_uid"), &SkeletonDefinition::_get_preview_mesh_uid);
	ClassDB::bind_method(D_METHOD("set_bones", "bones"), &SkeletonDefinition::set_bones);
	ClassDB::bind_method(D_METHOD("_set_bones", "bones"), &SkeletonDefinition::_set_bones);
	ClassDB::bind_method(D_METHOD("get_bones"), &SkeletonDefinition::get_bones);
	ClassDB::bind_method(D_METHOD("get_bone_count"), &SkeletonDefinition::get_bone_count);
	ClassDB::bind_method(D_METHOD("find_bone", "name"), &SkeletonDefinition::find_bone);
	ClassDB::bind_method(D_METHOD("get_bone_name", "index"), &SkeletonDefinition::get_bone_name);
	ClassDB::bind_method(D_METHOD("get_bone_parent", "index"), &SkeletonDefinition::get_bone_parent);
	ClassDB::bind_method(D_METHOD("get_bone_rest", "index"), &SkeletonDefinition::get_bone_rest);
	ClassDB::bind_method(D_METHOD("compare", "other", "tolerance"), &SkeletonDefinition::compare, DEFVAL(0.0001));

	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "bones", PROPERTY_HINT_ARRAY_TYPE, "Dictionary", PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "_set_bones", "get_bones");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "preview_mesh", PROPERTY_HINT_FILE, "*.skmesh"), "set_preview_mesh", "get_preview_mesh");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "_preview_mesh_uid", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "_set_preview_mesh_uid", "_get_preview_mesh_uid");
}

void SkeletonDefinition::set_preview_mesh(const String &p_path) {
	preview_mesh = p_path;
	preview_mesh_uid = p_path.is_empty() ? ResourceUID::INVALID_ID : ResourceLoader::get_resource_uid(p_path);
	emit_changed();
}

String SkeletonDefinition::get_preview_mesh() const {
	if (preview_mesh_uid != ResourceUID::INVALID_ID && ResourceUID::get_singleton()->has_id(preview_mesh_uid)) {
		return ResourceUID::get_singleton()->get_id_path(preview_mesh_uid);
	}
	return preview_mesh;
}

void SkeletonDefinition::_set_preview_mesh_uid(int64_t p_uid) {
	preview_mesh_uid = p_uid;
}
int64_t SkeletonDefinition::_get_preview_mesh_uid() const {
	return preview_mesh_uid;
}
