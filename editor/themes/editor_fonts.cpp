/**************************************************************************/
/*  editor_fonts.cpp                                                      */
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

#include "editor_fonts.h"

#include "core/io/dir_access.h"
#include "core/os/os.h"
#include "core/string/translation_server.h"
#include "editor/editor_string_names.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/builtin_fonts.gen.h"
#include "editor/themes/editor_scale.h"
#include "modules/webview/web_bridge.h" // 默认字体解析路径运行时存储（WebBridge::set_resolved_fonts）
#include "scene/resources/font.h"
#include "scene/scene_string_names.h"

Ref<FontFile> load_external_font(const String &p_path, TextServer::Hinting p_hinting, TextServer::FontAntialiasing p_aa, bool p_autohint, TextServer::SubpixelPositioning p_font_subpixel_positioning, bool p_font_disable_embedded_bitmaps, bool p_msdf = false, TypedArray<Font> *r_fallbacks = nullptr) {
	Ref<FontFile> font;
	font.instantiate();

	Vector<uint8_t> data = FileAccess::get_file_as_bytes(p_path);

	font->set_data(data);
	font->set_multichannel_signed_distance_field(p_msdf);
	font->set_antialiasing(p_aa);
	font->set_hinting(p_hinting);
	font->set_force_autohinter(p_autohint);
	font->set_subpixel_positioning(p_font_subpixel_positioning);
	font->set_disable_embedded_bitmaps(p_font_disable_embedded_bitmaps);

	if (r_fallbacks != nullptr) {
		r_fallbacks->push_back(font);
	}

	return font;
}

Ref<SystemFont> load_system_font(const PackedStringArray &p_names, TextServer::Hinting p_hinting, TextServer::FontAntialiasing p_aa, bool p_autohint, TextServer::SubpixelPositioning p_font_subpixel_positioning, bool p_font_disable_embedded_bitmaps, bool p_msdf = false, TypedArray<Font> *r_fallbacks = nullptr) {
	Ref<SystemFont> font;
	font.instantiate();

	font->set_font_names(p_names);
	font->set_multichannel_signed_distance_field(p_msdf);
	font->set_antialiasing(p_aa);
	font->set_hinting(p_hinting);
	font->set_force_autohinter(p_autohint);
	font->set_subpixel_positioning(p_font_subpixel_positioning);
	font->set_disable_embedded_bitmaps(p_font_disable_embedded_bitmaps);

	if (r_fallbacks != nullptr) {
		r_fallbacks->push_back(font);
	}

	return font;
}

Ref<FontFile> load_internal_font(const uint8_t *p_data, size_t p_size, TextServer::Hinting p_hinting, TextServer::FontAntialiasing p_aa, bool p_autohint, TextServer::SubpixelPositioning p_font_subpixel_positioning, bool p_font_disable_embedded_bitmaps, bool p_msdf = false, TypedArray<Font> *r_fallbacks = nullptr) {
	Ref<FontFile> font;
	font.instantiate();

	font->set_data_ptr(p_data, p_size);
	font->set_multichannel_signed_distance_field(p_msdf);
	font->set_antialiasing(p_aa);
	font->set_hinting(p_hinting);
	font->set_force_autohinter(p_autohint);
	font->set_subpixel_positioning(p_font_subpixel_positioning);
	font->set_disable_embedded_bitmaps(p_font_disable_embedded_bitmaps);

	if (r_fallbacks != nullptr) {
		r_fallbacks->push_back(font);
	}

	return font;
}

Ref<FontVariation> make_bold_font(const Ref<Font> &p_font, double p_embolden, TypedArray<Font> *r_fallbacks = nullptr) {
	Ref<FontVariation> font_var;
	font_var.instantiate();
	font_var->set_base_font(p_font);
	font_var->set_variation_embolden(p_embolden);

	if (r_fallbacks != nullptr) {
		r_fallbacks->push_back(font_var);
	}

	return font_var;
}

// 默认字体加载信息（运行时快照）：editor_register_fonts 填充，
// editor_print_font_load_info 输出（立即 + 主窗口就绪后补打）。
struct EditorFontLoadInfo {
	bool valid = false;
	String path; // 实际加载路径（空 = 内置回退）
	int bytes = 0; // 真实读入字节数
	int default_font_size = 0; // main_font_size × EDSCALE（实际渲染字号）
	int main_font_size = 0;
	float edscale = 1.0f;
};
static EditorFontLoadInfo s_font_load_info;

// 输出默认字体加载信息（诊断级，DEV_ENABLED）。
void editor_print_font_load_info() {
	if (!s_font_load_info.valid) {
		return;
	}
#ifdef DEV_ENABLED
	if (s_font_load_info.path.is_empty()) {
		print_line("[editor-font] 默认字体回退内置 Inter（外部分发缺失/不可读）");
	} else {
		print_line("[editor-font] 默认字体已加载: " + s_font_load_info.path + " bytes=" + itos(s_font_load_info.bytes));
		print_line("[editor-font] 实际字号: " + itos(s_font_load_info.default_font_size) + "px (main_font_size=" + itos(s_font_load_info.main_font_size) + " × EDSCALE=" + String::num(s_font_load_info.edscale) + ")");
	}
#endif
}

void editor_register_fonts(const Ref<Theme> &p_theme) {
	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);

	TextServer::FontAntialiasing font_antialiasing = (TextServer::FontAntialiasing)(int)EDITOR_GET("interface/editor/fonts/font_antialiasing");
	int font_hinting_setting = (int)EDITOR_GET("interface/editor/fonts/font_hinting");
	TextServer::SubpixelPositioning font_subpixel_positioning = (TextServer::SubpixelPositioning)(int)EDITOR_GET("interface/editor/fonts/font_subpixel_positioning");
	bool font_disable_embedded_bitmaps = (bool)EDITOR_GET("interface/editor/fonts/font_disable_embedded_bitmaps");
	bool font_allow_msdf = (bool)EDITOR_GET("interface/editor/fonts/font_allow_msdf");

	TextServer::Hinting font_hinting;
	TextServer::Hinting font_mono_hinting;
	switch (font_hinting_setting) {
		case 0:
			// The "Auto" setting uses the setting that best matches the OS' font rendering:
			// - macOS doesn't use font hinting.
			// - Windows uses ClearType, which is in between "Light" and "Normal" hinting.
			// - Linux has configurable font hinting, but most distributions including Ubuntu default to "Light".
#ifdef MACOS_ENABLED
			font_hinting = TextServer::HINTING_NONE;
			font_mono_hinting = TextServer::HINTING_NONE;
#else
			font_hinting = TextServer::HINTING_LIGHT;
			font_mono_hinting = TextServer::HINTING_LIGHT;
#endif
			break;
		case 1:
			font_hinting = TextServer::HINTING_NONE;
			font_mono_hinting = TextServer::HINTING_NONE;
			break;
		case 2:
			font_hinting = TextServer::HINTING_LIGHT;
			font_mono_hinting = TextServer::HINTING_LIGHT;
			break;
		default:
			font_hinting = TextServer::HINTING_NORMAL;
			font_mono_hinting = TextServer::HINTING_LIGHT;
			break;
	}

	// Load built-in fonts.
	const int default_font_size = int(EDITOR_GET("interface/editor/fonts/main_font_size")) * EDSCALE;
	const float embolden_strength = 0.6;

	// 默认主字体：外部分发字体优先（bin/webview/ui/fonts/，与 WebDock 共享同一文件，
	// 两边字形一致；Noto Sans CJK SC = 思源黑体，SIL OFL）。缺失/不可读回退内置 Inter。
	// 实际生效路径经 WebBridge::set_resolved_fonts 写入运行时存储（非持久化——防机器
	// 绝对路径写入 editor_settings-*.tres），WebDock 桥读取，字体来源单一（此处决策）。
	const String bundled_main_font = OS::get_singleton()->get_executable_path().get_base_dir()
											  .path_join("webview/ui/fonts/NotoSansCJKsc-Regular.otf");
	const String bundled_bold_font = OS::get_singleton()->get_executable_path().get_base_dir()
											 .path_join("webview/ui/fonts/NotoSansCJKsc-Bold.otf");
	// 进程级 static 字节缓冲：normal+MSDF 共享同一份（避免 16MB 文件 4 次全读 + 64MB 缓冲，审查 E5）。
	static Vector<uint8_t> s_bundled_main_data;
	static Vector<uint8_t> s_bundled_bold_data;
	if (s_bundled_main_data.is_empty()) {
		s_bundled_main_data = FileAccess::get_file_as_bytes(bundled_main_font);
	}
	// 验证：字节非空才算加载成功（exists 只判存在，损坏/不可读文件需走回退，审查 E4）。
	const bool use_bundled_main_font = !s_bundled_main_data.is_empty();
	Ref<Font> default_font;
	Ref<Font> default_font_msdf;
	String resolved_main_font = ""; // 实际生效路径（运行时存储，供 WebDock 桥）
	String resolved_bold_font = "";
	if (use_bundled_main_font) {
		default_font = load_internal_font(s_bundled_main_data.ptr(), s_bundled_main_data.size(), font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false);
		default_font_msdf = load_internal_font(s_bundled_main_data.ptr(), s_bundled_main_data.size(), font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, font_allow_msdf);
		resolved_main_font = bundled_main_font;
		s_font_load_info = { true, bundled_main_font, static_cast<int>(s_bundled_main_data.size()), default_font_size, int(EDITOR_GET("interface/editor/fonts/main_font_size")), EDSCALE };
	} else {
		default_font = load_internal_font(_font_Inter_Regular, _font_Inter_Regular_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false);
		default_font_msdf = load_internal_font(_font_Inter_Regular, _font_Inter_Regular_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, font_allow_msdf);
		// 回退为异常部署（外部分发字体缺失/损坏）：无条件警告（review E4 留口，用户可确认）。
		WARN_PRINT("[editor-font] 默认字体回退内置 Inter：外部分发字体缺失或不可读 (" + bundled_main_font + ")");
		s_font_load_info = { true, String(), 0, default_font_size, int(EDITOR_GET("interface/editor/fonts/main_font_size")), EDSCALE };
	}
	editor_print_font_load_info(); // 立即输出（console exe 可见）；GUI 版由主窗口就绪后补打

	Dictionary default_features;
	default_features["calt"] = false; // Disable contextual alternates by default.
	default_features["ss04"] = true; // Serifed I, tailed l for better distinction.
	default_features["tnum"] = true; // Tabular numbers for better alignment.

	String noto_cjk_path;
	String noto_cjk_bold_path;
	{
		Vector<String> var_suffix;

		// Note: Most Noto Sans CJK versions support all glyph variations, but select the best matching one in case it's not.
		String locale = TranslationServer::get_singleton()->get_tool_locale();
		if (!locale.begins_with("zh") && !locale.begins_with("ja") && !locale.begins_with("ko")) {
			locale = OS::get_singleton()->get_locale();
		}
		if (locale.begins_with("zh") && (locale.contains("Hans") || locale.contains("CN") || locale.contains("SG"))) {
			var_suffix = { "SC", "TC", "HK", "KR", "JP" };
		} else if (locale.begins_with("zh") && locale.contains("HK")) {
			var_suffix = { "HK", "TC", "SC", "KR", "JP" };
		} else if (locale.begins_with("zh") && (locale.contains("Hant") || locale.contains("MO") || locale.contains("TW"))) {
			var_suffix = { "TC", "HK", "SC", "KR", "JP" };
		} else if (locale.begins_with("ko")) {
			var_suffix = { "KR", "HK", "SC", "TC", "JP" };
		} else if (locale.begins_with("ja")) {
			var_suffix = { "JP", "HK", "KR", "SC", "TC" };
		} else {
			var_suffix = { "HK", "KR", "SC", "TC", "JP" };
		}
		for (int64_t i = 0; i < var_suffix.size(); i++) {
			if (noto_cjk_path.is_empty()) {
				noto_cjk_path = OS::get_singleton()->get_system_font_path("Noto Sans CJK " + var_suffix[i], 400, 100);
			}
			if (noto_cjk_bold_path.is_empty()) {
				noto_cjk_bold_path = OS::get_singleton()->get_system_font_path("Noto Sans CJK " + var_suffix[i], 800, 100);
			}
		}
	}

	TypedArray<Font> fallbacks;
	Ref<FontFile> arabic_font = load_internal_font(_font_Vazirmatn_Regular, _font_Vazirmatn_Regular_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks);
	Ref<FontFile> bengali_font = load_internal_font(_font_NotoSansBengali_Regular, _font_NotoSansBengali_Regular_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks);
	Ref<FontFile> devanagari_font = load_internal_font(_font_NotoSansDevanagari_Regular, _font_NotoSansDevanagari_Regular_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks);
	Ref<FontFile> georgian_font = load_internal_font(_font_NotoSansGeorgian_Regular, _font_NotoSansGeorgian_Regular_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks);
	Ref<FontFile> hebrew_font = load_internal_font(_font_NotoSansHebrew_Regular, _font_NotoSansHebrew_Regular_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks);
	Ref<FontFile> malayalam_font = load_internal_font(_font_NotoSansMalayalamUI_Regular, _font_NotoSansMalayalamUI_Regular_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks);
	Ref<FontFile> oriya_font = load_internal_font(_font_NotoSansOriya_Regular, _font_NotoSansOriya_Regular_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks);
	Ref<FontFile> sinhala_font = load_internal_font(_font_NotoSansSinhala_Regular, _font_NotoSansSinhala_Regular_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks);
	Ref<FontFile> tamil_font = load_internal_font(_font_NotoSansTamilUI_Regular, _font_NotoSansTamilUI_Regular_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks);
	Ref<FontFile> telugu_font = load_internal_font(_font_NotoSansTeluguUI_Regular, _font_NotoSansTeluguUI_Regular_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks);
	Ref<FontFile> thai_font = load_internal_font(_font_NotoSansThai_Regular, _font_NotoSansThai_Regular_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks);
	if (!noto_cjk_path.is_empty()) {
		load_external_font(noto_cjk_path, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks);
	}
	if (use_bundled_main_font) {
		// 思源为主字体覆盖 CJK——无需 DroidSansFallback（内置数组随之编译期剔除，省 ~1.19MB）。
	} else {
		// Inter 回退：恢复嵌入式 CJK 回退（DroidSansFallback，zh/ko 覆盖）——否则回退路径
		// 中文/韩文缺字（DroidSansJapanese 显式禁用 zh/ko，审查 E1）。
		Ref<FontFile> fallback_font = load_internal_font(_font_DroidSansFallback, _font_DroidSansFallback_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks);
		fallback_font->set_language_support_override("ja", false);
		fallback_font->set_language_support_override("zh", true);
		fallback_font->set_language_support_override("ko", true);
		fallback_font->set_language_support_override("*", false);
	}
	Ref<FontFile> japanese_font = load_internal_font(_font_DroidSansJapanese, _font_DroidSansJapanese_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks);
	japanese_font->set_language_support_override("ja", true);
	japanese_font->set_language_support_override("zh", false);
	japanese_font->set_language_support_override("ko", false);
	japanese_font->set_language_support_override("*", false);
	default_font->set_fallbacks(fallbacks);
	default_font_msdf->set_fallbacks(fallbacks);

	Ref<Font> default_font_bold;
	Ref<Font> default_font_bold_msdf;
	if (use_bundled_main_font) {
		if (s_bundled_bold_data.is_empty()) {
			s_bundled_bold_data = FileAccess::get_file_as_bytes(bundled_bold_font);
		}
		if (!s_bundled_bold_data.is_empty()) {
			default_font_bold = load_internal_font(s_bundled_bold_data.ptr(), s_bundled_bold_data.size(), font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false);
			default_font_bold_msdf = load_internal_font(s_bundled_bold_data.ptr(), s_bundled_bold_data.size(), font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, font_allow_msdf);
			resolved_bold_font = bundled_bold_font;
		} else {
			// Bold 文件缺失/损坏：Regular + embolden 合成（与 fallback 粗体同机制），无独立路径。
			default_font_bold = make_bold_font(default_font, embolden_strength);
			default_font_bold_msdf = make_bold_font(default_font_msdf, embolden_strength);
		}
	} else {
		default_font_bold = load_internal_font(_font_Inter_Bold, _font_Inter_Bold_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false);
		default_font_bold_msdf = load_internal_font(_font_Inter_Bold, _font_Inter_Bold_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, font_allow_msdf);
	}
	// 解析路径写入运行时存储（Regular 回退时 bold 一并清空——防陈旧路径残留，审查 E3）。
	WebBridge::set_resolved_fonts(resolved_main_font, resolved_bold_font);

	TypedArray<Font> fallbacks_bold;
	Ref<FontFile> arabic_font_bold = load_internal_font(_font_Vazirmatn_Bold, _font_Vazirmatn_Bold_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks_bold);
	Ref<FontFile> bengali_font_bold = load_internal_font(_font_NotoSansBengali_Bold, _font_NotoSansBengali_Bold_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks_bold);
	Ref<FontFile> devanagari_font_bold = load_internal_font(_font_NotoSansDevanagari_Bold, _font_NotoSansDevanagari_Bold_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks_bold);
	Ref<FontFile> georgian_font_bold = load_internal_font(_font_NotoSansGeorgian_Bold, _font_NotoSansGeorgian_Bold_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks_bold);
	Ref<FontFile> hebrew_font_bold = load_internal_font(_font_NotoSansHebrew_Bold, _font_NotoSansHebrew_Bold_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks_bold);
	Ref<FontFile> malayalam_font_bold = load_internal_font(_font_NotoSansMalayalamUI_Bold, _font_NotoSansMalayalamUI_Bold_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks_bold);
	Ref<FontFile> oriya_font_bold = load_internal_font(_font_NotoSansOriya_Bold, _font_NotoSansOriya_Bold_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks_bold);
	Ref<FontFile> sinhala_font_bold = load_internal_font(_font_NotoSansSinhala_Bold, _font_NotoSansSinhala_Bold_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks_bold);
	Ref<FontFile> tamil_font_bold = load_internal_font(_font_NotoSansTamilUI_Bold, _font_NotoSansTamilUI_Bold_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks_bold);
	Ref<FontFile> telugu_font_bold = load_internal_font(_font_NotoSansTeluguUI_Bold, _font_NotoSansTeluguUI_Bold_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks_bold);
	Ref<FontFile> thai_font_bold = load_internal_font(_font_NotoSansThai_Bold, _font_NotoSansThai_Bold_size, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks_bold);
	if (!noto_cjk_bold_path.is_empty()) {
		load_external_font(noto_cjk_bold_path, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false, &fallbacks_bold);
	}
	Ref<FontVariation> japanese_font_bold = make_bold_font(japanese_font, embolden_strength, &fallbacks_bold);

	if (OS::get_singleton()->has_feature("system_fonts")) {
		PackedStringArray emoji_font_names = {
			"Apple Color Emoji",
			"Segoe UI Emoji",
			"Noto Color Emoji",
			"Twitter Color Emoji",
			"OpenMoji",
			"EmojiOne Color"
		};
		Ref<SystemFont> emoji_font = load_system_font(emoji_font_names, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, false);
		fallbacks.push_back(emoji_font);
		fallbacks_bold.push_back(emoji_font);
	}

	default_font_bold->set_fallbacks(fallbacks_bold);
	default_font_bold_msdf->set_fallbacks(fallbacks_bold);

	Ref<FontFile> default_font_mono = load_internal_font(_font_JetBrainsMono_Regular, _font_JetBrainsMono_Regular_size, font_mono_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps);
	default_font_mono->set_subpixel_positioning(TextServer::SUBPIXEL_POSITIONING_DISABLED);
	default_font_mono->set_keep_rounding_remainders(false);
	default_font_mono->set_fallbacks(fallbacks);

	// Init base font configs and load custom fonts.
	String custom_font_path = EDITOR_GET("interface/editor/fonts/main_font");
	String custom_font_path_bold = EDITOR_GET("interface/editor/fonts/main_font_bold");
	String custom_font_path_source = EDITOR_GET("interface/editor/fonts/code_font");

	Ref<FontVariation> default_fc;
	default_fc.instantiate();
	if (custom_font_path.length() > 0 && dir->file_exists(custom_font_path)) {
		Ref<FontFile> custom_font = load_external_font(custom_font_path, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps);
		{
			TypedArray<Font> fallback_custom = { default_font };
			custom_font->set_fallbacks(fallback_custom);
		}
		default_fc->set_base_font(custom_font);
	} else {
		EditorSettings::get_singleton()->set_manually("interface/editor/fonts/main_font", "");
		default_fc->set_opentype_features(default_features);
		default_fc->set_base_font(default_font);
	}
	default_fc->set_spacing(TextServer::SPACING_TOP, -EDSCALE);
	default_fc->set_spacing(TextServer::SPACING_BOTTOM, -EDSCALE);
	Dictionary default_fc_opentype;
	default_fc_opentype["weight"] = 400;
	default_fc->set_variation_opentype(default_fc_opentype);

	Ref<FontVariation> default_fc_msdf;
	default_fc_msdf.instantiate();
	if (custom_font_path.length() > 0 && dir->file_exists(custom_font_path)) {
		Ref<FontFile> custom_font = load_external_font(custom_font_path, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, font_allow_msdf);
		{
			TypedArray<Font> fallback_custom = { default_font_msdf };
			custom_font->set_fallbacks(fallback_custom);
		}
		default_fc_msdf->set_base_font(custom_font);
	} else {
		EditorSettings::get_singleton()->set_manually("interface/editor/fonts/main_font", "");
		default_fc_msdf->set_opentype_features(default_features);
		default_fc_msdf->set_base_font(default_font_msdf);
	}
	default_fc_msdf->set_spacing(TextServer::SPACING_TOP, -EDSCALE);
	default_fc_msdf->set_spacing(TextServer::SPACING_BOTTOM, -EDSCALE);
	default_fc_msdf->set_variation_opentype(default_fc_opentype);

	Ref<FontVariation> bold_fc;
	bold_fc.instantiate();
	if (custom_font_path_bold.length() > 0 && dir->file_exists(custom_font_path_bold)) {
		Ref<FontFile> custom_font = load_external_font(custom_font_path_bold, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps);
		{
			TypedArray<Font> fallback_custom = { default_font_bold };
			custom_font->set_fallbacks(fallback_custom);
		}
		bold_fc->set_base_font(custom_font);
	} else if (custom_font_path.length() > 0 && dir->file_exists(custom_font_path)) {
		Ref<FontFile> custom_font = load_external_font(custom_font_path, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps);
		{
			TypedArray<Font> fallback_custom = { default_font_bold };
			custom_font->set_fallbacks(fallback_custom);
		}
		bold_fc->set_base_font(custom_font);
		if (!custom_font->get_supported_variation_list().has(TS->name_to_tag("wght"))) {
			bold_fc->set_variation_embolden(embolden_strength);
		}
	} else {
		EditorSettings::get_singleton()->set_manually("interface/editor/fonts/main_font_bold", "");
		bold_fc->set_opentype_features(default_features);
		bold_fc->set_base_font(default_font_bold);
	}
	bold_fc->set_spacing(TextServer::SPACING_TOP, -EDSCALE);
	bold_fc->set_spacing(TextServer::SPACING_BOTTOM, -EDSCALE);
	Dictionary bold_fc_opentype;
	bold_fc_opentype["weight"] = 700;
	bold_fc->set_variation_opentype(bold_fc_opentype);

	Ref<FontVariation> bold_fc_msdf;
	bold_fc_msdf.instantiate();
	if (custom_font_path_bold.length() > 0 && dir->file_exists(custom_font_path_bold)) {
		Ref<FontFile> custom_font = load_external_font(custom_font_path_bold, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, font_allow_msdf);
		{
			TypedArray<Font> fallback_custom = { default_font_bold_msdf };
			custom_font->set_fallbacks(fallback_custom);
		}
		bold_fc_msdf->set_base_font(custom_font);
	} else if (custom_font_path.length() > 0 && dir->file_exists(custom_font_path)) {
		Ref<FontFile> custom_font = load_external_font(custom_font_path, font_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps, font_allow_msdf);
		{
			TypedArray<Font> fallback_custom = { default_font_bold_msdf };
			custom_font->set_fallbacks(fallback_custom);
		}
		bold_fc_msdf->set_base_font(custom_font);
		if (!custom_font->get_supported_variation_list().has(TS->name_to_tag("wght"))) {
			bold_fc_msdf->set_variation_embolden(embolden_strength);
		}
	} else {
		EditorSettings::get_singleton()->set_manually("interface/editor/fonts/main_font_bold", "");
		bold_fc_msdf->set_opentype_features(default_features);
		bold_fc_msdf->set_base_font(default_font_bold_msdf);
	}
	bold_fc_msdf->set_spacing(TextServer::SPACING_TOP, -EDSCALE);
	bold_fc_msdf->set_spacing(TextServer::SPACING_BOTTOM, -EDSCALE);
	bold_fc_msdf->set_variation_opentype(bold_fc_opentype);

	if (!String(EDITOR_GET("interface/editor/fonts/main_font_custom_opentype_features")).is_empty()) {
		Vector<String> subtag = String(EDITOR_GET("interface/editor/fonts/main_font_custom_opentype_features")).split(",");
		if (!subtag.is_empty()) {
			Dictionary ftrs;
			for (int i = 0; i < subtag.size(); i++) {
				Vector<String> subtag_a = subtag[i].split("=");
				if (subtag_a.size() == 2) {
					ftrs[TS->name_to_tag(subtag_a[0])] = subtag_a[1].to_int();
				} else if (subtag_a.size() == 1) {
					ftrs[TS->name_to_tag(subtag_a[0])] = 1;
				}
			}
			default_fc->set_opentype_features(ftrs);
			default_fc_msdf->set_opentype_features(ftrs);
			bold_fc->set_opentype_features(ftrs);
			bold_fc_msdf->set_opentype_features(ftrs);
		}
	}

	Ref<FontVariation> mono_fc;
	mono_fc.instantiate();
	if (custom_font_path_source.length() > 0 && dir->file_exists(custom_font_path_source)) {
		Ref<FontFile> custom_font = load_external_font(custom_font_path_source, font_mono_hinting, font_antialiasing, true, font_subpixel_positioning, font_disable_embedded_bitmaps);
		custom_font->set_subpixel_positioning(TextServer::SUBPIXEL_POSITIONING_DISABLED);
		custom_font->set_keep_rounding_remainders(false);
		{
			TypedArray<Font> fallback_custom = { default_font_mono };
			custom_font->set_fallbacks(fallback_custom);
		}
		mono_fc->set_base_font(custom_font);
	} else {
		EditorSettings::get_singleton()->set_manually("interface/editor/fonts/code_font", "");
		mono_fc->set_base_font(default_font_mono);
	}
	mono_fc->set_spacing(TextServer::SPACING_TOP, -EDSCALE);
	mono_fc->set_spacing(TextServer::SPACING_BOTTOM, -EDSCALE);

	Ref<FontVariation> mono_other_fc = mono_fc->duplicate();

	// Enable contextual alternates (coding ligatures) and custom features for the source editor font.
	int ot_mode = EDITOR_GET("interface/editor/fonts/code_font_contextual_ligatures");
	switch (ot_mode) {
		case 1: { // Disable ligatures.
			Dictionary ftrs;
			ftrs[TS->name_to_tag("calt")] = 0;
			mono_fc->set_opentype_features(ftrs);
		} break;
		case 2: { // Custom.
			Vector<String> subtag = String(EDITOR_GET("interface/editor/fonts/code_font_custom_opentype_features")).split(",");
			Dictionary ftrs;
			for (int i = 0; i < subtag.size(); i++) {
				Vector<String> subtag_a = subtag[i].split("=");
				if (subtag_a.size() == 2) {
					ftrs[TS->name_to_tag(subtag_a[0])] = subtag_a[1].to_int();
				} else if (subtag_a.size() == 1) {
					ftrs[TS->name_to_tag(subtag_a[0])] = 1;
				}
			}
			mono_fc->set_opentype_features(ftrs);
		} break;
		default: { // Enabled.
			Dictionary ftrs;
			ftrs[TS->name_to_tag("calt")] = 1;
			mono_fc->set_opentype_features(ftrs);
		} break;
	}

	Vector<String> variation_tags = String(EDITOR_GET("interface/editor/fonts/code_font_custom_variations")).split(",");
	Dictionary variations_mono;
	for (int i = 0; i < variation_tags.size(); i++) {
		Vector<String> subtag_a = variation_tags[i].split("=");
		if (subtag_a.size() == 2) {
			variations_mono[TS->name_to_tag(subtag_a[0])] = subtag_a[1].to_float();
		} else if (subtag_a.size() == 1) {
			variations_mono[TS->name_to_tag(subtag_a[0])] = 1;
		}
	}
	if (!variations_mono.is_empty()) {
		mono_fc->set_variation_opentype(variations_mono);
	}

	{
		// Disable contextual alternates (coding ligatures).
		Dictionary ftrs;
		ftrs[TS->name_to_tag("calt")] = 0;
		mono_other_fc->set_opentype_features(ftrs);
	}

	// Use fake bold/italics to style the editor log's `print_rich()` output.
	// Use stronger embolden strength to make bold easier to distinguish from regular text.
	Ref<FontVariation> mono_other_fc_bold = mono_other_fc->duplicate();
	mono_other_fc_bold->set_variation_embolden(0.8);

	Ref<FontVariation> mono_other_fc_italic = mono_other_fc->duplicate();
	mono_other_fc_italic->set_variation_transform(Transform2D(1.0, 0.2, 0.0, 1.0, 0.0, 0.0));

	Ref<FontVariation> mono_other_fc_bold_italic = mono_other_fc->duplicate();
	mono_other_fc_bold_italic->set_variation_embolden(0.8);
	mono_other_fc_bold_italic->set_variation_transform(Transform2D(1.0, 0.2, 0.0, 1.0, 0.0, 0.0));

	Ref<FontVariation> mono_other_fc_mono = mono_other_fc->duplicate();
	// Use a different font style to distinguish `[code]` in rich prints.
	// This emulates the "faint" styling used in ANSI escape codes by using a slightly thinner font.
	mono_other_fc_mono->set_variation_embolden(-0.25);
	mono_other_fc_mono->set_variation_transform(Transform2D(1.0, 0.1, 0.0, 1.0, 0.0, 0.0));

	Ref<FontVariation> italic_fc = default_fc->duplicate();
	italic_fc->set_variation_transform(Transform2D(1.0, 0.2, 0.0, 1.0, 0.0, 0.0));

	Ref<FontVariation> bold_italic_fc = bold_fc->duplicate();
	bold_italic_fc->set_variation_transform(Transform2D(1.0, 0.2, 0.0, 1.0, 0.0, 0.0));

	// Setup theme.

	p_theme->set_default_font(default_fc); // Default theme font config.
	p_theme->set_default_font_size(default_font_size);

	// Main font.

	p_theme->set_font("main", EditorStringName(EditorFonts), default_fc);
	p_theme->set_font("main_msdf", EditorStringName(EditorFonts), default_fc_msdf);
	p_theme->set_font_size("main_size", EditorStringName(EditorFonts), default_font_size);

	p_theme->set_font("bold", EditorStringName(EditorFonts), bold_fc);
	p_theme->set_font("main_bold_msdf", EditorStringName(EditorFonts), bold_fc_msdf);
	p_theme->set_font_size("bold_size", EditorStringName(EditorFonts), default_font_size);

	p_theme->set_font("italic", EditorStringName(EditorFonts), italic_fc);
	p_theme->set_font_size("italic_size", EditorStringName(EditorFonts), default_font_size);

	// Title font.

	p_theme->set_font("title", EditorStringName(EditorFonts), bold_fc);
	p_theme->set_font_size("title_size", EditorStringName(EditorFonts), default_font_size + 1 * EDSCALE);

	p_theme->set_type_variation("MainScreenButton", "Button");
	p_theme->set_font(SceneStringName(font), "MainScreenButton", bold_fc);
	p_theme->set_font_size(SceneStringName(font_size), "MainScreenButton", default_font_size + 2 * EDSCALE);

	// Labels.

	p_theme->set_font(SceneStringName(font), "Label", default_fc);

	p_theme->set_type_variation("HeaderSmall", "Label");
	p_theme->set_font(SceneStringName(font), "HeaderSmall", bold_fc);
	p_theme->set_font_size(SceneStringName(font_size), "HeaderSmall", default_font_size);

	p_theme->set_type_variation("HeaderMedium", "Label");
	p_theme->set_font(SceneStringName(font), "HeaderMedium", bold_fc);
	p_theme->set_font_size(SceneStringName(font_size), "HeaderMedium", default_font_size + 1 * EDSCALE);

	p_theme->set_type_variation("HeaderLarge", "Label");
	p_theme->set_font(SceneStringName(font), "HeaderLarge", bold_fc);
	p_theme->set_font_size(SceneStringName(font_size), "HeaderLarge", default_font_size + 3 * EDSCALE);

	p_theme->set_font("normal_font", "RichTextLabel", default_fc);
	p_theme->set_font("bold_font", "RichTextLabel", bold_fc);
	p_theme->set_font("italics_font", "RichTextLabel", italic_fc);
	p_theme->set_font("bold_italics_font", "RichTextLabel", bold_italic_fc);

	// Documentation fonts
	p_theme->set_font_size("doc_size", EditorStringName(EditorFonts), int(EDITOR_GET("text_editor/help/help_font_size")) * EDSCALE);
	p_theme->set_font("doc", EditorStringName(EditorFonts), default_fc);
	p_theme->set_font("doc_bold", EditorStringName(EditorFonts), bold_fc);
	p_theme->set_font("doc_italic", EditorStringName(EditorFonts), italic_fc);
	p_theme->set_font_size("doc_title_size", EditorStringName(EditorFonts), int(EDITOR_GET("text_editor/help/help_title_font_size")) * EDSCALE);
	p_theme->set_font("doc_title", EditorStringName(EditorFonts), bold_fc);
	p_theme->set_font_size("doc_source_size", EditorStringName(EditorFonts), int(EDITOR_GET("text_editor/help/help_source_font_size")) * EDSCALE);
	p_theme->set_font("doc_source", EditorStringName(EditorFonts), mono_fc);
	p_theme->set_font_size("doc_keyboard_size", EditorStringName(EditorFonts), (int(EDITOR_GET("text_editor/help/help_source_font_size")) - 1) * EDSCALE);
	p_theme->set_font("doc_keyboard", EditorStringName(EditorFonts), mono_fc);

	// Ruler font
	p_theme->set_font_size("rulers_size", EditorStringName(EditorFonts), 8 * EDSCALE);
	p_theme->set_font("rulers", EditorStringName(EditorFonts), default_fc);

	// Rotation widget font
	p_theme->set_font_size("rotation_control_size", EditorStringName(EditorFonts), 13 * EDSCALE);
	p_theme->set_font("rotation_control", EditorStringName(EditorFonts), default_fc);

	// Code font
	p_theme->set_font_size("source_size", EditorStringName(EditorFonts), int(EDITOR_GET("interface/editor/fonts/code_font_size")) * EDSCALE);
	p_theme->set_font("source", EditorStringName(EditorFonts), mono_fc);

	p_theme->set_font_size("expression_size", EditorStringName(EditorFonts), (int(EDITOR_GET("interface/editor/fonts/code_font_size")) - 1) * EDSCALE);
	p_theme->set_font("expression", EditorStringName(EditorFonts), mono_other_fc);

	p_theme->set_font_size("output_source_size", EditorStringName(EditorFonts), int(EDITOR_GET("run/output/font_size")) * EDSCALE);
	p_theme->set_font("output_source", EditorStringName(EditorFonts), mono_other_fc);
	p_theme->set_font("output_source_bold", EditorStringName(EditorFonts), mono_other_fc_bold);
	p_theme->set_font("output_source_italic", EditorStringName(EditorFonts), mono_other_fc_italic);
	p_theme->set_font("output_source_bold_italic", EditorStringName(EditorFonts), mono_other_fc_bold_italic);
	p_theme->set_font("output_source_mono", EditorStringName(EditorFonts), mono_other_fc_mono);

	p_theme->set_font_size("status_source_size", EditorStringName(EditorFonts), default_font_size);
	p_theme->set_font("status_source", EditorStringName(EditorFonts), mono_other_fc);
}
