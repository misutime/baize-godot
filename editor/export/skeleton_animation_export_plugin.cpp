/**************************************************************************/
/*  skeleton_animation_export_plugin.cpp                                  */
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

#include "skeleton_animation_export_plugin.h"

#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "editor/file_system/editor_file_system.h"
#include "scene/resources/skeleton_definition.h"

static void hash_clip_dependencies(EditorFileSystemDirectory *p_directory, uint32_t &r_hash) {
	for (int i = 0; i < p_directory->get_file_count(); i++) {
		const String path = p_directory->get_file_path(i);
		if (path.get_extension().to_lower() == "skanim") {
			r_hash = hash_murmur3_one_64(path.hash64(), r_hash);
			r_hash = hash_murmur3_one_64(FileAccess::get_md5(path).hash64(), r_hash);
		}
	}
	for (int i = 0; i < p_directory->get_subdir_count(); i++) {
		hash_clip_dependencies(p_directory->get_subdir(i), r_hash);
	}
}

uint64_t SkeletonAnimationExportPlugin::_get_customization_configuration_hash() const {
	// Rebinding a clip changes the baked library even when its path stays the same.
	uint32_t hash = HASH_MURMUR3_SEED;
	EditorFileSystemDirectory *filesystem = EditorFileSystem::get_singleton()->get_filesystem();
	if (filesystem) {
		hash_clip_dependencies(filesystem, hash);
	}
	return hash;
}

bool SkeletonAnimationExportPlugin::_begin_customize_resources(const Ref<EditorExportPlatform> &p_platform, const Vector<String> &p_features) {
	return true;
}

Ref<Resource> SkeletonAnimationExportPlugin::_customize_resource(const Ref<Resource> &p_resource, const String &p_path) {
	// Only the skeleton file itself is rewritten. A skeleton reached through another resource
	// is the live editor object and must keep scanning its directory.
	Ref<SkeletonDefinition> exported = p_resource;
	if (exported.is_null() || p_path.is_empty() || exported->has_baked_animation_libraries()) {
		return Ref<Resource>();
	}
	Ref<SkeletonDefinition> source = ResourceLoader::load(p_path, "SkeletonDefinition");
	if (source.is_null() || source == exported) {
		return Ref<Resource>();
	}
	exported->set_baked_animation_libraries(source->bake_animation_libraries());
	return exported;
}
