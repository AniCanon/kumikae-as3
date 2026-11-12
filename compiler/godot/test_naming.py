"""Tests for the naming rules. Run: python3 -m unittest discover -s compiler/godot -p 'test_*.py'"""
import os
import re
import unittest

import naming


class Camel(unittest.TestCase):
    def test_methods(self):
        self.assertEqual(naming.camel("move_and_slide"), "moveAndSlide")
        self.assertEqual(naming.camel("add_child"), "addChild")
        self.assertEqual(naming.camel("position"), "position")
        self.assertEqual(naming.camel("get_node_3d"), "getNode3d")
        self.assertEqual(naming.camel("set_use_aabb"), "setUseAabb")

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

    def test_keywords_are_the_compilers(self):
        token_h = os.path.join(os.path.dirname(__file__), "..", "src", "token.h")
        with open(token_h) as file:
            words = re.findall(r"X\(Kw\w+, \"'(\w+)'\"\)", file.read())
        self.assertGreater(len(words), 30)
        self.assertEqual(sorted(naming.KEYWORDS), sorted(words))


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
