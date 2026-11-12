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
	result["missing_bones"] = missing_bones;
	result["extra_bones"] = extra_bones;
	result["parent_mismatches"] = parent_mismatches;
	result["rest_mismatches"] = rest_mismatches;
	return result;
}

SkeletonDefinition::~SkeletonDefinition() {}

Ref<AnimationLibrary> SkeletonDefinition::_get_stored_animation_library() const {
	return animation_library;
}

void SkeletonDefinition::set_animation_library(const Ref<AnimationLibrary> &p_library) {
	animation_library = p_library;
	emit_changed();
}

Ref<AnimationLibrary> SkeletonDefinition::get_animation_library() const {
	if (animation_library.is_null()) {
		animation_library.instantiate();
	}
	return animation_library;
}

Error SkeletonDefinition::prepare_for_save(const String &p_path) {
	Ref<AnimationLibrary> library = get_animation_library();
	LocalVector<StringName> names;
	library->get_animation_list(&names);
	for (const StringName &animation_name : names) {
		Ref<Animation> clip = library->get_animation(animation_name);
		ERR_FAIL_COND_V_MSG(clip->is_built_in() || !clip->get_path().has_extension("anim"), ERR_INVALID_DATA, "Save each skeleton clip to a standalone .anim file before adding it to the skeleton.");
		ERR_FAIL_COND_V_MSG(clip->get_skeleton_path() != p_path, ERR_INVALID_DATA, "Animation belongs to another skeleton.");
	}
	library->set_scene_unique_id("AnimationLibrary_shared");
	library->set_path(p_path + "::AnimationLibrary_shared", true);
	return OK;
}

void SkeletonDefinition::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_animation_library", "library"), &SkeletonDefinition::set_animation_library);
	ClassDB::bind_method(D_METHOD("get_animation_library"), &SkeletonDefinition::get_animation_library);
	ClassDB::bind_method(D_METHOD("_get_stored_animation_library"), &SkeletonDefinition::_get_stored_animation_library);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "animation_library", PROPERTY_HINT_RESOURCE_TYPE, "AnimationLibrary", PROPERTY_USAGE_STORAGE), "set_animation_library", "_get_stored_animation_library");
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
