/**************************************************************************/
/*  skeleton_definition.h                                                 */
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

#include "core/io/resource.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/variant/typed_array.h"

class Animation;
class AnimationLibrary;

class SkeletonDefinition : public Resource {
	GDCLASS(SkeletonDefinition, Resource);

	struct Bone {
		StringName name;
		int parent = -1;
		Transform3D rest;
	};
	Vector<Bone> bones;
	HashMap<StringName, int> bone_indices;
	// Clip files below the animation directory are the only catalog. Their names are the
	// runtime animation names and each sub-directory is one animation library.
	String animation_directory;
	// Written by the exporter only, so an exported game needs no directory listing.
	Dictionary baked_libraries;
	mutable HashMap<StringName, Ref<AnimationLibrary>> scanned_libraries;
	mutable Ref<AnimationLibrary> fallback_library;
	mutable TypedArray<Dictionary> scan_issues;
	mutable String scanned_root;
	mutable bool scanned = false;
	bool _scan(bool p_force) const;
	void _scan_directory(const String &p_directory, HashMap<StringName, Ref<Animation>> &r_clips) const;
	void _add_scan_issue(const String &p_path, const String &p_reason, const String &p_message) const;
	String preview_mesh;
	int64_t preview_mesh_uid = -1;
	void _set_preview_mesh_uid(int64_t p_uid);
	int64_t _get_preview_mesh_uid() const;

	void _set_bones(const TypedArray<Dictionary> &p_bones);

protected:
	static void _bind_methods();
	bool _set(const StringName &p_name, const Variant &p_value);

public:
	void set_animation_directory(const String &p_directory);
	String get_animation_directory() const;
	String get_animation_directory_path() const;
	void set_baked_animation_libraries(const Dictionary &p_libraries);
	Dictionary get_baked_animation_libraries() const;
	bool has_baked_animation_libraries() const;
	Dictionary bake_animation_libraries() const;
	Ref<AnimationLibrary> get_animation_library(const StringName &p_library = StringName()) const;
	void get_animation_library_list(LocalVector<StringName> *p_libraries) const;
	TypedArray<StringName> get_animation_library_names() const;
	bool rescan_animations();
	TypedArray<Dictionary> get_animation_scan_issues() const;
	TypedArray<Dictionary> get_animation_catalog() const;
	Error prepare_for_save(const String &p_path);
	~SkeletonDefinition();
	Error set_bones(const TypedArray<Dictionary> &p_bones);
	TypedArray<Dictionary> get_bones() const;
	int get_bone_count() const;
	int find_bone(const StringName &p_name) const;
	StringName get_bone_name(int p_index) const;
	int get_bone_parent(int p_index) const;
	Transform3D get_bone_rest(int p_index) const;

	Dictionary compare(const Ref<SkeletonDefinition> &p_other, real_t p_tolerance = 0.0001) const;

	void set_preview_mesh(const String &p_path);
	String get_preview_mesh() const;
};
