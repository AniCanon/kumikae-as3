"""Tests for the naming rules. Run: python3 -m unittest discover -s compiler/godot -p 'test_*.py'"""
from pathlib import Path
import runpy
import shutil
import tempfile
import unittest

import naming


class Camel(unittest.TestCase):
    def test_methods(self):
        self.assertEqual(naming.camel("move_and_slide"), "moveAndSlide")
        self.assertEqual(naming.camel("add_child"), "addChild")
        self.assertEqual(naming.camel("position"), "position")
        self.assertEqual(naming.camel("get_node_3d"), "getNode3D")
        self.assertEqual(naming.camel("set_use_aabb"), "setUseAABB")

    def test_acronyms_keep_their_capitals(self):
        # As Flash writes them (errorID, URLRequest), after the first word.
        self.assertEqual(naming.camel("get_aabb"), "getAABB")
        self.assertEqual(naming.camel("get_instance_id"), "getInstanceID")
        self.assertEqual(naming.camel("get_rid"), "getRID")
        self.assertEqual(naming.camel("to_utf8_buffer"), "toUTF8Buffer")
        self.assertEqual(naming.camel("get_world_2d"), "getWorld2D")
        self.assertEqual(naming.camel("request_url"), "requestURL")
        # The first word stays lower-case, as every member's does.
        self.assertEqual(naming.camel("id"), "id")
        self.assertEqual(naming.camel("rid_create"), "ridCreate")
        self.assertEqual(naming.camel("uv"), "uv")
        # Not a word that merely starts like one.
        self.assertEqual(naming.camel("get_identity"), "getIdentity")
        # Back to Godot's name, for a script's own member.
        self.assertEqual(naming.snake("getAABB"), "get_aabb")
        self.assertEqual(naming.snake("getInstanceID"), "get_instance_id")
        self.assertEqual(naming.snake("getWorld2D"), "get_world_2d")
        self.assertEqual(naming.snake("toUTF8Buffer"), "to_utf8_buffer")

    def test_leading_underscores_stay(self):
        self.assertEqual(naming.camel("_ready"), "_ready")
        self.assertEqual(naming.camel("_physics_process"), "_physicsProcess")
        self.assertEqual(naming.camel("__meta_key"), "__metaKey")

    def test_stray_underscores_vanish(self):
        self.assertEqual(naming.camel("a__b"), "aB")
        self.assertEqual(naming.camel("trailing_"), "trailing")

    def test_upper_case_names_are_kept(self):
        self.assertEqual(naming.camel("PROCESS_MODE_INHERIT"), "PROCESS_MODE_INHERIT")


class Safe(unittest.TestCase):
    def test_reserved_words_get_an_underscore(self):
        self.assertEqual(naming.safe("class"), "class_")
        self.assertEqual(naming.safe("default"), "default_")
        self.assertEqual(naming.safe("new"), "new_")
        self.assertEqual(naming.safe("get"), "get")
        self.assertEqual(naming.safe("node"), "node")

    def test_member_is_camel_then_safe(self):
        self.assertEqual(naming.member("is_class"), "isClass")
        self.assertEqual(naming.member("new"), "new_")
        self.assertEqual(naming.member("from_node"), "fromNode")

    def test_escaping_follows_the_compiler_token_spelling(self):
        with tempfile.TemporaryDirectory() as folder:
            compiler = Path(folder)
            (compiler / "src").mkdir()
            (compiler / "godot").mkdir()
            module = Path(naming.__file__)
            header = (module.parents[1] / "src/token.h").read_text(encoding="utf-8")
            (compiler / "src/token.h").write_text(header.replace("\"'class'\"", "\"'record'\""), encoding="utf-8")
            copy = shutil.copy(module, compiler / "godot")
            safe = runpy.run_path(copy)["safe"]
            self.assertEqual(safe("record"), "record_")
            self.assertEqual(safe("class"), "class")

    def test_an_unreadable_keyword_table_stops_generation(self):
        with tempfile.TemporaryDirectory() as folder:
            header = Path(folder) / "token.h"
            for table, message in [('X(Identifier, "identifier")', "No reserved words"),
                                   ('''X(KwIf, "'if'")\nX(KwClass, "class")''', "Unrecognized keyword")]:
                with self.subTest(table=table):
                    header.write_text(table, encoding="utf-8")
                    with self.assertRaisesRegex(ValueError, message):
                        naming._reserved_words(header)


class Classes(unittest.TestCase):
    def test_clashing_names_get_a_prefix(self):
        for name in ("Object", "Array", "Dictionary", "JSON", "Error"):
            self.assertEqual(naming.class_name(name), "Godot" + name)

    def test_other_names_are_kept(self):
        for name in ("Node2D", "HTTPRequest", "AABB", "CPUParticles2D", "Vector2i"):
            self.assertEqual(naming.class_name(name), name)

    def test_dotted_enum_names_lose_the_dot(self):
        self.assertEqual(naming.class_name("Variant.Type"), "VariantType")


class EnumValues(unittest.TestCase):
    def test_common_prefix_is_removed(self):
        self.assertEqual(naming.enum_values(["SIDE_LEFT", "SIDE_TOP"]), ["LEFT", "TOP"])
        self.assertEqual(naming.enum_values(["MOUSE_BUTTON_LEFT", "MOUSE_BUTTON_WHEEL_UP"]), ["LEFT", "WHEEL_UP"])

    def test_prefix_ends_on_an_underscore(self):
        self.assertEqual(naming.enum_values(["KEY_MASK_CTRL", "KEY_MODIFIER_MASK"]), ["MASK_CTRL", "MODIFIER_MASK"])
        self.assertEqual(naming.enum_values(["CLOCKWISE", "COUNTERCLOCKWISE"]), ["CLOCKWISE", "COUNTERCLOCKWISE"])

    def test_a_digit_keeps_the_full_name(self):
        self.assertEqual(naming.enum_values(["KEY_ESCAPE", "KEY_0", "KEY_A"]), ["ESCAPE", "KEY_0", "A"])

    def test_no_common_prefix(self):
        self.assertEqual(naming.enum_values(["OK", "FAILED", "ERR_BUG"]), ["OK", "FAILED", "ERR_BUG"])

    def test_one_value_keeps_its_name(self):
        self.assertEqual(naming.enum_values(["ONLY_ONE"]), ["ONLY_ONE"])

    def test_a_value_that_is_the_whole_prefix_stops_it(self):
        self.assertEqual(naming.enum_values(["TYPE", "TYPE_A"]), ["TYPE", "TYPE_A"])


class Snake(unittest.TestCase):
    def test_script_members(self):
        self.assertEqual(naming.snake("maxSpeed"), "max_speed")
        self.assertEqual(naming.snake("takeDamage"), "take_damage")
        self.assertEqual(naming.snake("_physicsProcess"), "_physics_process")
        self.assertEqual(naming.snake("loadURL"), "load_url")
        self.assertEqual(naming.snake("URLLoader"), "url_loader")
        self.assertEqual(naming.snake("x"), "x")


if __name__ == "__main__":
    unittest.main()
