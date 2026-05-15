/**************************************************************************/
/*  test_node_3d.cpp                                                      */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_node_3d)

#ifndef _3D_DISABLED

#include "scene/3d/node_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

namespace TestNode3D {

TEST_CASE("[SceneTree][Node3D][CoordinateSystem] look_at uses local +Y as forward and +Z as up") {
	// 坐标系迁移哨兵：Node3D.look_at 后，本地 +Y 指向目标，本地 +Z 保持上方向。
	Node3D *test_node = memnew(Node3D);
	SceneTree::get_singleton()->get_root()->add_child(test_node);

	test_node->set_global_position(Vector3(2, 3, 4));
	test_node->look_at(Vector3(2, 13, 4), Vector3(0, 0, 1));

	const Basis basis = test_node->get_global_basis();
	CHECK_MESSAGE(basis.get_column(Vector3::AXIS_X).is_equal_approx(Vector3(1, 0, 0)), "Local +X should point right.");
	CHECK_MESSAGE(basis.get_column(Vector3::AXIS_Y).is_equal_approx(Vector3(0, 1, 0)), "Local +Y should point toward the target.");
	CHECK_MESSAGE(basis.get_column(Vector3::AXIS_Z).is_equal_approx(Vector3(0, 0, 1)), "Local +Z should point up.");

	memdelete(test_node);
}

} // namespace TestNode3D

#endif // _3D_DISABLED
