extends MainLoop

const WARNING_PREFIX := "gdscript/warnings/"


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	var policy: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(args[1]))
	var warnings: Dictionary = policy["warnings"]

	if args[0] == "adopt":
		var config := ConfigFile.new()
		var load_error := config.load("res://project.godot")
		if load_error != OK:
			push_error(error_string(load_error))
			return

		for key: String in warnings:
			var value: Variant = warnings[key]
			# JSON numbers arrive as floats, while warning levels are integer settings.
			config.set_value(
				"debug", WARNING_PREFIX + key, type_convert(value, TYPE_INT) if value is float else value
			)

		var save_error := config.save(args[2])
		if save_error != OK:
			push_error(error_string(save_error))
		return

	var settings := {}
	for key: String in warnings:
		settings[key] = ProjectSettings.get_setting_with_override("debug/" + WARNING_PREFIX + key)
	var rules: Variant = ProjectSettings.get_setting_with_override(
		"debug/" + WARNING_PREFIX + "directory_rules"
	)
	var output := FileAccess.open(args[2], FileAccess.WRITE)
	if output == null:
		push_error(error_string(FileAccess.get_open_error()))
		return
	output.store_string(JSON.stringify({"warnings": settings, "directory_rules": rules}))


func _process(_delta: float) -> bool:
	return true
