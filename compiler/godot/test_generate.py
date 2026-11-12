"""Tests for the declaration generator.

The fixture tests always run. The tests of Godot's real API need its description, which is not in this
repository: set KAS3_GODOT_API to an extension_api.json, or keep one at spike/api/extension_api.json.
The tests that run the compiler need KAS3C, the path to kas3c; CTest sets it.
"""
import copy
import json
import os
import subprocess
import tempfile
import unittest

import generate

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
KAS3C = os.environ.get("KAS3C")
REAL_API = os.environ.get("KAS3_GODOT_API") or os.path.join(REPO, "spike", "api", "extension_api.json")


def load(path):
    with open(path) as file:
        return json.load(file)


def read(path):
    with open(path) as file:
        return file.read()


def fixture():
    return load(os.path.join(HERE, "fixture_api.json"))


def lines_of(text, class_name):
    """The member lines of one generated class."""
    result = []
    inside = False
    for line in text.split("\n"):
        if inside and line == "\t}":
            return result
        if inside:
            result.append(line.strip())
        elif (" class %s " % class_name) in line + " ":
            inside = True
    raise AssertionError("no class " + class_name)


class Fixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = generate.generate(fixture())

    def members(self, class_name):
        return lines_of(self.text, class_name)

    def test_whole_output(self):
        self.maxDiff = None
        self.assertEqual(self.text, read(os.path.join(HERE, "fixture.as")))

    def test_classes(self):
        self.assertIn("\tpublic native class GodotObject {", self.text)
        self.assertIn("\tpublic native class Node extends GodotObject {", self.text)
        self.assertIn("\tpublic native class GodotJSON extends GodotObject {", self.text)
        self.assertIn("\tpublic final native class Vector2 {", self.text)

    def test_as3_types_are_not_declared(self):
        for name in ("Nil", "int", "String", "Callable"):
            self.assertNotIn(" class %s " % name, self.text)
        self.assertNotIn("toUpper", self.text)

    def test_constructor_only_when_instantiable(self):
        self.assertIn("public native function Node();", self.members("Node"))
        self.assertFalse([line for line in self.members("Texture2D") if "function Texture2D(" in line])

    def test_methods(self):
        node = self.members("Node")
        self.assertIn("public native function getNode(path:String):Node;", node)
        self.assertIn("public native function isInsideTree():Boolean;", node)
        self.assertIn("public static native function printOrphanNodes():void;", node)
        self.assertIn("public native function getIndex(includeInternal:Boolean = false):int;", node)

    def test_virtuals_keep_the_underscore(self):
        node = self.members("Node")
        self.assertIn("public native function _ready():void;", node)
        self.assertIn("public native function _physicsProcess(delta:Number):void;", node)

    def test_varargs(self):
        self.assertIn("public native function call(method:String, ...rest):*;", self.members("GodotObject"))
        self.assertIn("public native function new_(...rest):*;", self.members("GodotJSON"))

    def test_keyword_parameter(self):
        self.assertIn("public native function isClass(class_:String):Boolean;", self.members("GodotObject"))

    def test_methods_named_get_and_set(self):
        god = self.members("GodotObject")
        self.assertIn("public native function get(property:String):*;", god)
        self.assertIn("public native function set(property:String, value:*):void;", god)

    def test_pointer_methods_are_left_out(self):
        self.assertNotIn("setOwnerPtr", self.text)

    def test_wide_integers_are_numbers(self):
        self.assertIn("public native function getInstanceId():Number;", self.members("GodotObject"))
        self.assertIn("public static native function getTicks():Number;", self.members("Input"))

    def test_defaults(self):
        self.assertIn(
            'public native function setTint(color:Color = null, label:String = "hi", group:String = "", '
            "scale:Number = 1.0, callback:Function = null, extra:* = null, items:GodotArray = null):void;",
            self.members("Node"))

    def test_properties_replace_their_accessors(self):
        node = self.members("Node")
        self.assertIn("public native function get name():String;", node)
        self.assertIn("public native function set name(value:String):void;", node)
        self.assertFalse([line for line in node if "getName" in line or "setName" in line])

    def test_property_type_without_a_described_getter(self):
        self.assertIn("public native function get owner():Node;", self.members("Node"))
        self.assertIn("public native function get data():GodotArray;", self.members("Control"))

    def test_indexed_property_keeps_its_accessors(self):
        control = self.members("Control")
        self.assertIn("public native function get anchorLeft():Number;", control)
        self.assertIn("public native function set anchorLeft(value:Number):void;", control)
        self.assertIn("public native function setAnchor(side:int, anchor:Number):void;", control)
        self.assertIn("public native function getAnchor(side:int):Number;", control)

    def test_read_only_property(self):
        control = self.members("Control")
        self.assertIn("public native function get size():Vector2;", control)
        self.assertFalse([line for line in control if "set size(" in line])

    def test_property_type_from_a_list(self):
        self.assertIn("public native function get theme():Texture2D;", self.members("Control"))

    def test_signals_are_properties(self):
        self.assertIn("public native function get ready():Signal;", self.members("Node"))
        self.assertIn("public native function get childEnteredTree():Signal;", self.members("Node"))

    def test_constants_and_enum_values(self):
        node = self.members("Node")
        self.assertIn("public static const NOTIFICATION_READY:int = 13;", node)
        self.assertIn("public static const PROCESS_MODE_ALWAYS:int = 3;", node)

    def test_enums_are_recorded(self):
        self.assertIn('\t[GodotEnum(name="ProcessMode", values="PROCESS_MODE_INHERIT,PROCESS_MODE_ALWAYS")]\n', self.text)
        node = self.members("Node")
        at = node.index("public native function addChild(node:Node, forceReadableName:Boolean = false, "
                        "internal_:int = 0):void;")
        self.assertEqual(node[at - 1], '[Godot(internal_="enum:Node.InternalMode")]')
        at = node.index("public native function get processMode():int;")
        self.assertEqual(node[at - 1], '[Godot(returns="enum:Node.ProcessMode")]')

    def test_typed_arrays_are_recorded(self):
        node = self.members("Node")
        at = node.index("public native function getChildren(includeInternal:Boolean = false):GodotArray;")
        self.assertEqual(node[at - 1], '[Godot(returns="Array[Node]")]')

    def test_global_enum_parameter_names_its_class(self):
        node = self.members("Node")
        at = node.index("public native function rpc(method:String, ...rest):int;")
        self.assertEqual(node[at - 1], '[Godot(returns="enum:GodotError")]')

    def test_singleton_members_are_static(self):
        input_ = self.members("Input")
        self.assertIn("public static native function isActionPressed(action:String, exactMatch:Boolean = false)"
                      ":Boolean;", input_)
        self.assertIn("public static native function get mouseMode():int;", input_)
        self.assertIn("public static native function set mouseMode(value:int):void;", input_)
        self.assertIn("public static native function get joyConnectionChanged():Signal;", input_)

    def test_wide_bitfield_is_a_number(self):
        self.assertIn("public static native function setFlags(flags:Number):void;", self.members("Input"))
        self.assertIn("public static const HIGH:Number = 34359738368;", self.members("Wide"))
        self.assertIn("public static const LOW:Number = 1;", self.members("Wide"))

    def test_global_enums(self):
        self.assertIn("\tpublic final native class Key {", self.text)
        self.assertEqual(self.members("Key"), [
            "public static const ESCAPE:int = 4194305;",
            "public static const KEY_0:int = 48;",
            "public static const A:int = 65;",
        ])
        self.assertIn("public static const OK:int = 0;", self.members("GodotError"))
        self.assertIn("public static const NIL:int = 0;", self.members("VariantType"))

    def test_built_in_class(self):
        vector = self.members("Vector2")
        self.assertIn("public var x:Number;", vector)
        self.assertIn("public static const ZERO:Vector2;", vector)
        self.assertIn("public static const AXIS_COUNT:int = 2;", vector)
        self.assertIn("public static const AXIS_Y:int = 1;", vector)
        self.assertIn("public native function distanceTo(to:Vector2):Number;", vector)
        self.assertIn("public static native function fromAngle(angle:Number):Vector2;", vector)

    def test_constructors_merge(self):
        self.assertIn("public native function Vector2(x:* = null, y:Number = 0);", self.members("Vector2"))
        self.assertIn("public native function Signal(object:* = null, signal:String = null);",
                      self.members("Signal"))
        self.assertIn("public native function GodotArray();", self.members("GodotArray"))

    def test_signal_class(self):
        signal = self.members("Signal")
        self.assertIn("public native function connect(callable:Function, flags:int = 0):int;", signal)
        self.assertIn("public native function emit(...rest):void;", signal)

    def test_global_functions(self):
        self.assertIn("\tpublic native function print(arg1:*, ...rest):void;\n", self.text)
        self.assertIn("\tpublic native function lerp(from:*, to:*, weight:*):*;\n", self.text)
        self.assertIn("\tpublic native function degToRad(deg:Number):Number;\n", self.text)

    def test_functions_as3_already_has_are_left_out(self):
        self.assertNotIn("typeof", self.text)
        self.assertNotIn("isFinite", self.text)

    def test_global_constants_are_left_out(self):
        self.assertNotIn("INT32_MAX", self.text)


class Refusals(unittest.TestCase):
    """The generator stops on what it does not understand."""

    def refuses(self, api, message):
        with self.assertRaises(generate.Refused) as caught:
            generate.generate(api)
        self.assertIn(message, str(caught.exception))

    def node(self, api):
        return [c for c in api["classes"] if c["name"] == "Node"][0]

    def test_unknown_type(self):
        api = fixture()
        self.node(api)["methods"][3]["arguments"][0]["type"] = "Mystery"
        self.refuses(api, "unknown type 'Mystery' in Node.add_child")

    def test_unknown_type_prefix(self):
        api = fixture()
        self.node(api)["methods"][3]["arguments"][0]["type"] = "weird::Node"
        self.refuses(api, "unknown type 'weird::Node'")

    def test_two_members_with_one_name(self):
        api = fixture()
        self.node(api)["methods"].append(copy.deepcopy(self.node(api)["methods"][5]))
        self.node(api)["methods"][-1]["name"] = "get__node"
        self.refuses(api, "Node: 'getNode' names both")

    def test_member_clashing_with_an_inherited_one(self):
        api = fixture()
        self.node(api)["signals"].append({"name": "get_class"})
        self.refuses(api, "Node: 'getClass' names both")

    def test_class_named_like_an_as3_built_in(self):
        api = fixture()
        api["classes"].append({"name": "Date", "is_refcounted": False, "is_instantiable": True,
                               "inherits": "Object", "api_type": "core"})
        self.refuses(api, "'Date' is also an AS3 name")

    def test_function_named_like_an_as3_built_in(self):
        api = fixture()
        api["utility_functions"].append({"name": "trace", "category": "general", "is_vararg": True, "hash": 1})
        self.refuses(api, "'trace' is also an AS3 name")

    def test_two_classes_with_one_name(self):
        api = fixture()
        api["global_enums"].append({"name": "Node", "is_bitfield": False, "values": [{"name": "A", "value": 0}]})
        self.refuses(api, "two definitions named 'Node'")

    def test_two_parameters_with_one_name(self):
        api = fixture()
        self.node(api)["methods"][3]["arguments"][1]["name"] = "node"
        self.refuses(api, "Node.add_child: two parameters named 'node'")

    def test_unknown_default(self):
        api = fixture()
        self.node(api)["methods"][3]["arguments"][1]["default_value"] = "maybe"
        self.refuses(api, "Node.add_child: can't write the default 'maybe'")


@unittest.skipUnless(KAS3C, "KAS3C is not set")
class FixtureCompiles(unittest.TestCase):
    USER = """package {
	import godot.*;

	public class Player extends Control {
		[Export] public var maxSpeed:Number = 300;
		[Signal] public var died:Signal;

		override public function _ready():void {
			(getNode("Hitbox") as Node).childEnteredTree.connect(onEntered);
			processMode = Node.PROCESS_MODE_ALWAYS;
			var at:Vector2 = new Vector2(1, 2);
			at.x = Vector2.ZERO.distanceTo(new Vector2());
			var id:Number = getInstanceId();
			if (Input.isActionPressed("jump") && Key.ESCAPE != Key.KEY_0) {
				print("jump", degToRad(90), anchorLeft, size.y, name);
			}
			addChild(new Node(), true);
			Input.mouseMode = 1;
			Input.joyConnectionChanged.connect(onEntered);
			var children:GodotArray = getChildren();
			var error:int = rpc("go", 1, 2);
			if (error != GodotError.OK) { died.emit(); }
		}

		override public function _physicsProcess(delta:Number):void {}

		private function onEntered(node:Node):void {}
	}
}
"""

    def check(self, user, *flags):
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "Player.as")
            with open(path, "w") as file:
                file.write(user)
            done = subprocess.run([KAS3C, "check", "--no-godot", "--declarations",
                                   os.path.join(HERE, "fixture.as"), *flags, path],
                                  capture_output=True, text=True, timeout=60)
            return done.stderr.replace(folder + os.sep, "")

    def test_the_fixture_alone_is_clean(self):
        self.assertEqual(self.check("package { public class Player {} }"), "")

    def test_a_program_that_uses_each_shape(self):
        self.assertEqual(self.check(self.USER), "")

    def test_wrong_uses_are_reported(self):
        wrong = self.USER.replace("_physicsProcess(delta:Number)", "_physicsProcess(delta:int)")
        wrong = wrong.replace("addChild(new Node(), true)", "add_child(new Node())")
        wrong = wrong.replace("Input.mouseMode = 1", "size = at")
        self.assertEqual(self.check(wrong), "\n".join([
            "Player.as:17:4: error: undefined name 'add_child'",
            "Player.as:18:4: error: 'size' is read-only",
            "Player.as:25:28: error: '_physicsProcess' must have the same parameters and return type as "
            "'_physicsProcess' in Node",
            ""]))


@unittest.skipUnless(os.path.exists(REAL_API), "Godot's API description is not here: see this file's first lines")
class RealApi(unittest.TestCase):
    """Godot's whole API. A new Godot version that breaks a naming rule fails here."""

    @classmethod
    def setUpClass(cls):
        cls.api = load(REAL_API)
        cls.generator = generate.Generator(cls.api)
        cls.text = cls.generator.run()

    def test_the_committed_file_is_current(self):
        self.assertTrue(self.text == read(os.path.join(HERE, "godot.as")),
                        "godot.as is stale: run generate.py again")

    def test_only_the_listed_classes_are_renamed(self):
        names = [entry["name"] for group in ("classes", "builtin_classes", "global_enums")
                 for entry in self.api[group]]
        renamed = sorted(name for name in names if generate.naming.class_name(name) != name.replace(".", ""))
        self.assertEqual(renamed, ["Array", "Dictionary", "Error", "JSON", "Object"])

    def test_no_definition_is_an_as3_name(self):
        self.assertEqual(self.generator.defined & generate.as3_names(), set())

    def test_one_method_is_named_like_a_keyword(self):
        import re
        self.assertEqual(re.findall(r"function (\w+_)\(", self.text), ["new_"])

    def test_what_is_left_out(self):
        self.assertEqual(len(self.generator.left_out), 60)
        declared = [line for line in self.text.split("\n") if line.startswith("\tpublic native function ")]
        self.assertEqual(len(declared), len(self.api["utility_functions"]) - 2)

    def test_every_class_is_declared(self):
        declared = self.text.count(" native class ")
        hidden = len(generate.AS3_STANDS_FOR) - 1  # Variant is not a built-in class.
        self.assertEqual(declared, len(self.api["classes"]) + len(self.api["builtin_classes"]) - hidden +
                         len(self.api["global_enums"]))

    def test_the_names_the_design_promises(self):
        for line in ("public native function moveAndSlide():Boolean;",
                     "public native function _physicsProcess(delta:Number):void;",
                     "public native function get timeout():Signal;",
                     "public static const ESCAPE:int = 4194305;",
                     "public static const PROCESS_MODE_PAUSABLE:int = 1;",
                     "public static native function isActionPressed(action:String, exactMatch:Boolean = false)"
                     ":Boolean;",
                     "public native function get visible():Boolean;",
                     "public native function isOnFloor():Boolean;"):
            self.assertIn("\t\t" + line + "\n", self.text)
        self.assertNotIn("isVisible()", self.text)
        self.assertIn("\tpublic native function degToRad(deg:Number):Number;\n", self.text)

    @unittest.skipUnless(KAS3C, "KAS3C is not set")
    def test_the_compiler_finds_nothing_wrong_with_it(self):
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "Empty.as")
            with open(path, "w") as file:
                file.write("package { public class Empty {} }\n")
            done = subprocess.run([KAS3C, "check", path], capture_output=True, text=True, timeout=60)
            self.assertEqual(done.stderr, "")
            self.assertEqual(done.returncode, 0)


if __name__ == "__main__":
    unittest.main()
