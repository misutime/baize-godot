/**************************************************************************/
/*  coordinate_system.h                                                   */
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

#include "core/math/transform_3d.h"

namespace CoordinateSystem3D {

// 场景/API 对外使用 +Y 前、+Z 上；部分底层协议暂时仍使用 -Z 前、+Y 上。
inline Vector3 scene_to_legacy_z_forward_local(const Vector3 &p_vector) {
	return Vector3(p_vector.x, p_vector.z, -p_vector.y);
}

inline Vector3 legacy_z_forward_to_scene_local(const Vector3 &p_vector) {
	return Vector3(p_vector.x, -p_vector.z, p_vector.y);
}

inline Basis scene_to_legacy_z_forward_basis(const Basis &p_basis) {
	Basis legacy_basis;
	legacy_basis.set_columns(p_basis.get_column(Vector3::AXIS_X), p_basis.get_column(Vector3::AXIS_Z), -p_basis.get_column(Vector3::AXIS_Y));
	return legacy_basis;
}

inline Transform3D scene_to_legacy_z_forward_transform(const Transform3D &p_transform) {
	Transform3D legacy_transform = p_transform;
	legacy_transform.basis = scene_to_legacy_z_forward_basis(p_transform.basis);
	return legacy_transform;
}

} // namespace CoordinateSystem3D
