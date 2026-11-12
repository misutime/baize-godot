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
#include "core/variant/typed_array.h"

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
	mutable Ref<AnimationLibrary> animation_library;
	String preview_mesh;
	int64_t preview_mesh_uid = -1;
	void _set_preview_mesh_uid(int64_t p_uid);
	int64_t _get_preview_mesh_uid() const;

	void _set_bones(const TypedArray<Dictionary> &p_bones);

protected:
	static void _bind_methods();

public:
	void set_animation_library(const Ref<AnimationLibrary> &p_library);
	Ref<AnimationLibrary> get_animation_library() const;
	Ref<AnimationLibrary> _get_stored_animation_library() const;
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
