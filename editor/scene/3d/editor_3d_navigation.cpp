/**************************************************************************/
/*  editor_3d_navigation.cpp                                              */
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

#include "editor_3d_navigation.h"

#include "core/input/input.h"
#include "core/input/shortcut.h"
#include "editor/settings/editor_settings.h"
#include "scene/gui/control.h"

namespace Editor3DNavigation {

void configure_controller(const Ref<View3DController> &p_controller, bool p_update_all) {
	p_controller->set_shortcut(View3DController::SHORTCUT_FOV_DECREASE, ED_GET_SHORTCUT("spatial_editor/decrease_fov"));
	p_controller->set_shortcut(View3DController::SHORTCUT_FOV_INCREASE, ED_GET_SHORTCUT("spatial_editor/increase_fov"));
	p_controller->set_shortcut(View3DController::SHORTCUT_FOV_RESET, ED_GET_SHORTCUT("spatial_editor/reset_fov"));

	p_controller->set_shortcut(View3DController::SHORTCUT_PAN_MOD_1, ED_GET_SHORTCUT("spatial_editor/viewport_pan_modifier_1"));
	p_controller->set_shortcut(View3DController::SHORTCUT_PAN_MOD_2, ED_GET_SHORTCUT("spatial_editor/viewport_pan_modifier_2"));

	p_controller->set_shortcut(View3DController::SHORTCUT_ORBIT_MOD_1, ED_GET_SHORTCUT("spatial_editor/viewport_orbit_modifier_1"));
	p_controller->set_shortcut(View3DController::SHORTCUT_ORBIT_MOD_2, ED_GET_SHORTCUT("spatial_editor/viewport_orbit_modifier_2"));
	p_controller->set_shortcut(View3DController::SHORTCUT_ORBIT_SNAP_MOD_1, ED_GET_SHORTCUT("spatial_editor/viewport_orbit_snap_modifier_1"));
	p_controller->set_shortcut(View3DController::SHORTCUT_ORBIT_SNAP_MOD_2, ED_GET_SHORTCUT("spatial_editor/viewport_orbit_snap_modifier_2"));

	p_controller->set_shortcut(View3DController::SHORTCUT_FREELOOK_FORWARD, ED_GET_SHORTCUT("spatial_editor/freelook_forward"));
	p_controller->set_shortcut(View3DController::SHORTCUT_FREELOOK_BACKWARDS, ED_GET_SHORTCUT("spatial_editor/freelook_backwards"));
	p_controller->set_shortcut(View3DController::SHORTCUT_FREELOOK_LEFT, ED_GET_SHORTCUT("spatial_editor/freelook_left"));
	p_controller->set_shortcut(View3DController::SHORTCUT_FREELOOK_RIGHT, ED_GET_SHORTCUT("spatial_editor/freelook_right"));
	p_controller->set_shortcut(View3DController::SHORTCUT_FREELOOK_UP, ED_GET_SHORTCUT("spatial_editor/freelook_up"));
	p_controller->set_shortcut(View3DController::SHORTCUT_FREELOOK_DOWN, ED_GET_SHORTCUT("spatial_editor/freelook_down"));
	p_controller->set_shortcut(View3DController::SHORTCUT_FREELOOK_SPEED_MOD, ED_GET_SHORTCUT("spatial_editor/freelook_speed_modifier"));
	p_controller->set_shortcut(View3DController::SHORTCUT_FREELOOK_SLOW_MOD, ED_GET_SHORTCUT("spatial_editor/freelook_slow_modifier"));

	p_controller->set_shortcut(View3DController::SHORTCUT_ZOOM_MOD_1, ED_GET_SHORTCUT("spatial_editor/viewport_zoom_modifier_1"));
	p_controller->set_shortcut(View3DController::SHORTCUT_ZOOM_MOD_2, ED_GET_SHORTCUT("spatial_editor/viewport_zoom_modifier_2"));

	if (p_update_all || EditorSettings::get_singleton()->check_changed_settings_in_group("editors/3d")) {
		p_controller->set_pan_mouse_button((View3DController::NavigationMouseButton)EDITOR_GET("editors/3d/navigation/pan_mouse_button").operator int());

		p_controller->set_orbit_sensitivity(EDITOR_GET("editors/3d/navigation_feel/orbit_sensitivity"));
		p_controller->set_orbit_inertia(EDITOR_GET("editors/3d/navigation_feel/orbit_inertia"));
		p_controller->set_orbit_mouse_button((View3DController::NavigationMouseButton)EDITOR_GET("editors/3d/navigation/orbit_mouse_button").operator int());

		p_controller->set_zoom_style((View3DController::ZoomStyle)EDITOR_GET("editors/3d/navigation/zoom_style").operator int());
		p_controller->set_zoom_inertia(EDITOR_GET("editors/3d/navigation_feel/zoom_inertia"));
		p_controller->set_zoom_mouse_button((View3DController::NavigationMouseButton)EDITOR_GET("editors/3d/navigation/zoom_mouse_button").operator int());

		p_controller->set_freelook_scheme((View3DController::FreelookScheme)EDITOR_GET("editors/3d/freelook/freelook_navigation_scheme").operator int());
		p_controller->set_freelook_base_speed(EDITOR_GET("editors/3d/freelook/freelook_base_speed"));
		p_controller->set_freelook_sensitivity(EDITOR_GET("editors/3d/freelook/freelook_sensitivity"));
		p_controller->set_freelook_inertia(EDITOR_GET("editors/3d/freelook/freelook_inertia"));
		p_controller->set_freelook_speed_zoom_link(EDITOR_GET("editors/3d/freelook/freelook_speed_zoom_link"));
		p_controller->set_freelook_invert_y_axis(EDITOR_GET("editors/3d/freelook/freelook_invert_y_axis"));

		p_controller->set_translation_sensitivity(EDITOR_GET("editors/3d/navigation_feel/translation_sensitivity"));
		p_controller->set_translation_inertia(EDITOR_GET("editors/3d/navigation_feel/translation_inertia"));

		p_controller->set_angle_snap_threshold(EDITOR_GET("editors/3d/navigation_feel/angle_snap_threshold"));

		p_controller->set_emulate_3_button_mouse(EDITOR_GET("editors/3d/navigation/emulate_3_button_mouse"));
		p_controller->set_emulate_numpad(EDITOR_GET("editors/3d/navigation/emulate_numpad"));

		p_controller->set_invert_x_axis(EDITOR_GET("editors/3d/navigation/invert_x_axis"));
		p_controller->set_invert_y_axis(EDITOR_GET("editors/3d/navigation/invert_y_axis"));

		p_controller->set_warped_mouse_panning(EDITOR_GET("editors/3d/navigation/warped_mouse_panning"));
	}
}

static Key _get_key_modifier_setting(const String &p_property) {
	switch (EDITOR_GET(p_property).operator int()) {
		case 0:
			return Key::NONE;
		case 1:
			return Key::SHIFT;
		case 2:
			return Key::ALT;
		case 3:
			return Key::META;
		case 4:
			return Key::CTRL;
	}
	return Key::NONE;
}

Key get_key_modifier(const Ref<InputEventWithModifiers> &p_event) {
	if (p_event->is_shift_pressed()) {
		return Key::SHIFT;
	}
	if (p_event->is_alt_pressed()) {
		return Key::ALT;
	}
	if (p_event->is_ctrl_pressed()) {
		return Key::CTRL;
	}
	if (p_event->is_meta_pressed()) {
		return Key::META;
	}
	return Key::NONE;
}

static bool _is_nav_modifier_pressed(const String &p_name) {
	Ref<Shortcut> shortcut = ED_GET_SHORTCUT(p_name);
	ERR_FAIL_COND_V_MSG(shortcut.is_null(), true, "The Shortcut was null, possible name mismatch.");
	return shortcut->get_events().is_empty() || Input::get_singleton()->is_action_pressed(p_name);
}

bool has_navigation_mouse_binding(View3DController::NavigationMouseButton p_button) {
	return (EDITOR_GET("editors/3d/navigation/orbit_mouse_button").operator int() == p_button && _is_nav_modifier_pressed("spatial_editor/viewport_orbit_modifier_1") && _is_nav_modifier_pressed("spatial_editor/viewport_orbit_modifier_2")) ||
			(EDITOR_GET("editors/3d/navigation/pan_mouse_button").operator int() == p_button && _is_nav_modifier_pressed("spatial_editor/viewport_pan_modifier_1") && _is_nav_modifier_pressed("spatial_editor/viewport_pan_modifier_2")) ||
			(EDITOR_GET("editors/3d/navigation/zoom_mouse_button").operator int() == p_button && _is_nav_modifier_pressed("spatial_editor/viewport_zoom_modifier_1") && _is_nav_modifier_pressed("spatial_editor/viewport_zoom_modifier_2"));
}

bool handle_freelook_input(const Ref<View3DController> &p_controller, Control &p_surface, const Ref<InputEvent> &p_event, bool p_allow_start) {
	const bool can_start = p_allow_start && !p_controller->is_orthogonal();
	const Ref<InputEventMouseButton> button = p_event;
	if (button.is_valid() && button->get_button_index() == MouseButton::RIGHT) {
		if (button->is_pressed()) {
			if (!can_start || button->is_alt_pressed() || has_navigation_mouse_binding(View3DController::NAV_MOUSE_BUTTON_RIGHT) || get_key_modifier(button) != _get_key_modifier_setting("editors/3d/freelook/freelook_activation_modifier")) {
				return false;
			}
			p_controller->set_freelook_enabled(true);
			if (!p_surface.has_focus()) {
				p_surface.grab_focus();
			}
			return true;
		}
		if (p_controller->is_freelook_enabled()) {
			p_controller->set_freelook_enabled(false);
			return true;
		}
	}

	const Ref<InputEventKey> key_event = p_event;
	if (key_event.is_valid() && key_event->is_pressed()) {
		if (key_event->get_keycode() == Key::ESCAPE && p_controller->is_freelook_enabled()) {
			p_controller->set_freelook_enabled(false);
			return true;
		}
		if (can_start && !key_event->is_echo() && ED_IS_SHORTCUT("spatial_editor/freelook_toggle", p_event)) {
			p_controller->set_freelook_enabled(!p_controller->is_freelook_enabled());
			return true;
		}
	}
	return false;
}

} //namespace Editor3DNavigation
