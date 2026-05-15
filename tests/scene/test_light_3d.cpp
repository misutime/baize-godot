/**************************************************************************/
/*  test_light_3d.cpp                                                     */
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

TEST_FORCE_LINK(test_light_3d)

#ifndef _3D_DISABLED

#include "scene/3d/light_3d.h"

namespace TestLight3D {

TEST_CASE("[SceneTree][Light3D][CoordinateSystem] SpotLight3D AABB uses +Y forward") {
	SpotLight3D *light = memnew(SpotLight3D);
	light->set_param(Light3D::PARAM_RANGE, 10.0);
	light->set_param(Light3D::PARAM_SPOT_ANGLE, 30.0);

	const real_t cone_radius = Math::sin(Math::deg_to_rad(30.0)) * 10.0;
	const AABB expected_aabb(Vector3(-cone_radius, 0.0, -cone_radius), Vector3(cone_radius * 2.0, 10.0, cone_radius * 2.0));

	CHECK_MESSAGE(light->get_aabb().is_equal_approx(expected_aabb), "SpotLight3D 的本地包围盒应该沿 +Y 前方展开，并把 +Z 当作上下。");

	memdelete(light);
}

TEST_CASE("[SceneTree][Light3D][CoordinateSystem] AreaLight3D AABB uses X/Z face and +Y range") {
	AreaLight3D *light = memnew(AreaLight3D);
	light->set_param(Light3D::PARAM_RANGE, 4.0);
	light->set_area_size(Vector2(2.0, 6.0));

	const AABB expected_aabb(Vector3(-5.0, 0.0, -7.0), Vector3(10.0, 4.0, 14.0));

	CHECK_MESSAGE(light->get_aabb().is_equal_approx(expected_aabb), "AreaLight3D 的照射范围应该沿 +Y 前方展开，面片宽高分别落在 X/Z 轴。");

	memdelete(light);
}

} // namespace TestLight3D

#endif // _3D_DISABLED
