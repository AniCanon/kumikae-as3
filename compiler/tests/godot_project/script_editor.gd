extends SceneTree

var failed := false

func _initialize() -> void:
	run.call_deferred()

func check(condition: bool, message: String) -> void:
	if not condition:
		failed = true
		printerr("EDITOR CHECK: ", message)

func has_completion(code: CodeEdit, word: String, kind: int) -> bool:
	for index in code.get_code_completion_options().size():
		var option := code.get_code_completion_option(index)
		if option.display_text == word and option.kind == kind:
			code.set_code_completion_selected_index(index)
			return true
	return false

func ask(code: CodeEdit, line: int, column: int, force := true) -> void:
	code.cancel_code_completion()
	code.set_caret_line(line)
	code.set_caret_column(column)
	code.request_code_completion(force)

func run() -> void:
	var scripts := EditorInterface.get_script_editor()
	await process_frame
	while EditorInterface.get_resource_filesystem().is_scanning():
		await process_frame
	scripts.goto_help("class_name:Node")
	var helper := load("res://Plain.as") as Script
	EditorInterface.edit_script(helper)
	check(scripts.get_current_script() == helper, "a plain ActionScript helper opens in the Script editor")
	var script := load("res://Player.as") as Script
	EditorInterface.edit_script(script)
	var editor := scripts.get_current_editor()
	if editor == null or not editor.get_base_editor() is CodeEdit:
		printerr("EDITOR CHECK: Player.as did not open in the Script editor")
		quit(1)
		return
	var code := editor.get_base_editor() as CodeEdit
	var highlighter := code.syntax_highlighter
	var settings := EditorInterface.get_editor_settings()
	var keyword: Color = settings.get_setting("text_editor/theme/highlighting/keyword_color")
	check(highlighter.get_line_syntax_highlighting(0).get(0, {}).get("color") == keyword,
		"package is highlighted automatically")

	code.text = "package {\n  import godot.*;\n  public class Player extends CharacterBody2D {\n" \
		+ "    /* comment\n       still a comment */\n" \
		+ '    public function tryIt():void { var caption:String = "é"; var localSpeed:Number = 100; localSp\n' \
		+ "      this.moveAndSl\n      var other:Plain;\n      other.hel\n    }\n  }\n}"
	var comment: Color = settings.get_setting("text_editor/theme/highlighting/comment_color")
	check(highlighter.get_line_syntax_highlighting(4).get(0, {}).get("color") == comment,
		"a multiline comment stays colored on the next line")
	var string_color: Color = settings.get_setting("text_editor/theme/highlighting/string_color")
	check(highlighter.get_line_syntax_highlighting(5).get(code.get_line(5).find('"'), {}).get("color") == string_color,
		"a quoted string has the editor's string color")
	check(highlighter.get_line_syntax_highlighting(5).get(code.get_line(5).find("var localSpeed"), {}).get("color") == keyword,
		"highlighting columns after Unicode use Godot's character offsets")
	ask(code, 5, code.get_line(5).length(), false)
	check(has_completion(code, "localSpeed", CodeEdit.KIND_VARIABLE), "completion sees an unsaved local after Unicode")
	code.confirm_code_completion()
	check(code.get_line(5).ends_with("localSpeed"), "Godot inserts the selected completion")
	ask(code, 6, code.get_line(6).length())
	check(has_completion(code, "moveAndSlide", CodeEdit.KIND_FUNCTION), "completion sees an inherited Godot method")
	ask(code, 8, code.get_line(8).length())
	check(has_completion(code, "hello", CodeEdit.KIND_FUNCTION), "completion reads another ActionScript file")
	ask(code, 4, 12)
	check(code.get_code_completion_options().is_empty(), "comments do not show completions")

	code.set_line(5, '    public function tryIt():void { var pattern:RegExp = /"/; var ratio:Number = 4 / 2;')
	var pattern := code.get_line(5).find("/")
	var division := code.get_line(5).rfind("/")
	var syntax := highlighter.get_line_syntax_highlighting(5)
	var symbol: Color = settings.get_setting("text_editor/theme/highlighting/symbol_color")
	check(syntax.get(pattern, {}).get("color") == string_color, "an AS3 regular expression is colored as a literal")
	check(syntax.get(division, {}).get("color") == symbol, "a division operator is colored as a symbol")

	code.text = script.source_code + "// saved from the Script editor\n"
	scripts.save_all_scripts()
	check(FileAccess.get_file_as_string(script.resource_path).ends_with("// saved from the Script editor\n"),
		"Godot can save edits to an ActionScript file")
	print("EDITOR ", "failed" if failed else "passed")
	quit(1 if failed else 0)
