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

    def test_constructor_forms(self):
        # The merged constructor takes any of them; each form Godot has is written down, so that the
        # checker can refuse a call that matches none.
        self.assertIn('\t[Constructor(args="")]\n\t[Constructor(args="Vector2")]\n\t[Constructor(args="Number,Number")]\n',
                      self.text)

    def test_constructor_only_when_instantiable(self):
        self.assertIn("public native function Node();", self.members("Node"))
        self.assertFalse([line for line in self.members("Texture2D") if "function Texture2D(" in line])

    def test_methods(self):
        node = self.members("Node")
        self.assertIn("public final native function getNode(path:String):Node;", node)
        self.assertIn("public final native function isInsideTree():Boolean;", node)
        self.assertIn("public static native function printOrphanNodes():void;", node)
        self.assertIn("public final native function getIndex(includeInternal:Boolean = false):int;", node)

    def test_virtuals_keep_the_underscore(self):
        node = self.members("Node")
        self.assertIn("public native function _ready():void;", node)
        self.assertIn("public native function _physicsProcess(delta:Number):void;", node)

    def test_varargs(self):
        self.assertIn("public final native function call(method:String, ...rest):*;", self.members("GodotObject"))
        self.assertIn("public final native function new_(...rest):*;", self.members("GodotJSON"))

    def test_keyword_parameter(self):
        self.assertIn("public final native function isClass(class_:String):Boolean;", self.members("GodotObject"))

    def test_methods_named_get_and_set(self):
        god = self.members("GodotObject")
        self.assertIn("public final native function get(property:String):*;", god)
        self.assertIn("public final native function set(property:String, value:*):void;", god)

    def test_pointer_methods_are_left_out(self):
        self.assertNotIn("setOwnerPtr", self.text)

    def test_wide_integers_are_int64(self):
        self.assertIn("public final native function getInstanceID():Int64;", self.members("GodotObject"))
        self.assertIn("public static native function getTicks():Int64;", self.members("Input"))

    def test_defaults(self):
        self.assertIn(
            'public final native function setTint(color:Color = null, label:String = "hi", group:String = "", '
            "scale:Number = 1.0, callback:Function = null, extra:* = null, items:GodotArray = null):void;",
            self.members("Node"))

    def test_properties_replace_their_accessors(self):
        node = self.members("Node")
        self.assertIn("public final native function get name():String;", node)
        self.assertIn("public final native function set name(value:String):void;", node)
        self.assertFalse([line for line in node if "getName" in line or "setName" in line])

    def test_property_type_without_a_described_getter(self):
        self.assertIn("public final native function get owner():Node;", self.members("Node"))
        self.assertIn("public final native function get data():GodotArray;", self.members("Control"))

    def test_indexed_property_keeps_its_accessors(self):
        control = self.members("Control")
        self.assertIn("public final native function get anchorLeft():Number;", control)
        self.assertIn("public final native function set anchorLeft(value:Number):void;", control)
        self.assertIn("public final native function setAnchor(side:int, anchor:Number):void;", control)
        self.assertIn("public final native function getAnchor(side:int):Number;", control)

    def test_read_only_property(self):
        control = self.members("Control")
        self.assertIn("public final native function get size():Vector2;", control)
        self.assertFalse([line for line in control if "set size(" in line])

    def test_property_type_from_a_list(self):
        self.assertIn("public final native function get theme():Texture2D;", self.members("Control"))

    def test_signals_are_properties(self):
        self.assertIn("public final native function get ready():Signal;", self.members("Node"))
        self.assertIn("public final native function get childEnteredTree():Signal;", self.members("Node"))

    def test_constants_and_enum_values(self):
        node = self.members("Node")
        self.assertIn("public static const NOTIFICATION_READY:int = 13;", node)
        self.assertIn("public static const PROCESS_MODE_ALWAYS:int = 3;", node)

    def test_enums_are_recorded(self):
        self.assertIn('\t[GodotEnum(name="ProcessMode", values="PROCESS_MODE_INHERIT,PROCESS_MODE_ALWAYS")]\n', self.text)
        node = self.members("Node")
        at = node.index("public final native function addChild(node:Node, forceReadableName:Boolean = false, "
                        "internal_:int = 0):void;")
        self.assertEqual(node[at - 1], '[Godot(internal_="enum:Node.InternalMode")]')
        at = node.index("public final native function get processMode():int;")
        self.assertEqual(node[at - 1], '[Godot(returns="enum:Node.ProcessMode")]')

    def test_typed_arrays_are_recorded(self):
        node = self.members("Node")
        at = node.index("public final native function getChildren(includeInternal:Boolean = false):GodotArray;")
        self.assertEqual(node[at - 1], '[Godot(returns="Array[Node]")]')

    def test_global_enum_parameter_names_its_class(self):
        node = self.members("Node")
        at = node.index("public final native function rpc(method:String, ...rest):int;")
        self.assertEqual(node[at - 1], '[Godot(returns="enum:GodotError")]')

    def test_singleton_members_are_static(self):
        input_ = self.members("Input")
        self.assertIn("public static native function isActionPressed(action:String, exactMatch:Boolean = false)"
                      ":Boolean;", input_)
        self.assertIn("public static native function get mouseMode():Int64;", input_)
        self.assertIn("public static native function set mouseMode(value:Int64):void;", input_)
        self.assertIn("public static native function get joyConnectionChanged():Signal;", input_)

    def test_singleton_is_tagged_and_gives_itself(self):
        self.assertIn("public static native function get singleton():Input;", self.members("Input"))
        at = self.text.index("\tpublic native class Input ")
        self.assertIn("\t[Singleton]\n", self.text[at - 60:at])
        self.assertNotIn("singleton", " ".join(self.members("Node")))

    def test_wide_bitfield_is_an_int64(self):
        self.assertIn("public static native function setFlags(flags:Int64):void;", self.members("Input"))
        self.assertIn("public static const HIGH:Int64 = 34359738368;", self.members("Wide"))
        self.assertIn("public static const LOW:Int64 = 1;", self.members("Wide"))

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
        self.assertIn("public native function emit(...rest):void;", signal)

    def test_signal_listeners(self):
        # AS3's spelling beside Godot's: add, addOnce and remove.
        signal = self.members("Signal")
        self.assertIn("public native function add(listener:Function, deferred:Boolean = false):void;", signal)
        self.assertIn("public native function addOnce(listener:Function, deferred:Boolean = false):void;", signal)
        self.assertIn("public native function remove(listener:Function):void;", signal)
        self.assertIn("public native function removeAll():void;", signal)
        self.assertIn("public native function get numListeners():int;", signal)
        self.assertIn("public native function has(listener:Function):Boolean;", signal)
        # Godot's own spelling is not declared: one way to do it.
        self.assertEqual([line for line in signal if "connect" in line.lower()], [])

    def test_signal_listener_clash(self):
        # If Godot ever gives Signal a method of one of those names, the generator stops.
        api = fixture()
        for entry in api["builtin_classes"]:
            if entry["name"] == "Signal":
                entry["methods"].append({"name": "add_once", "is_vararg": False, "is_const": False, "is_static": False})
        with self.assertRaises(generate.Refused):
            generate.generate(api)

    def test_global_functions(self):
        self.assertIn("\tpublic native function print(...rest):void;\n", self.text)
        self.assertIn("\tpublic native function lerp(from:*, to:*, weight:*):*;\n", self.text)
        self.assertIn("\tpublic native function degToRad(deg:Number):Number;\n", self.text)

    def test_functions_as3_already_has_are_left_out(self):
        self.assertNotIn("typeof", self.text)
        self.assertNotIn("isFinite", self.text)

    def test_what_the_description_leaves_out_of_object(self):
        god = self.members("GodotObject")
        self.assertIn("public native function _notification(what:int):void;", god)
        self.assertNotIn("_toString", self.text)
        self.assertIn("public native function _get(property:String):*;", god)
        self.assertIn("public native function _set(property:String, value:*):Boolean;", god)
        self.assertIn("public final native function free():void;", god)

    def test_classes_that_cannot_be_created(self):
        self.assertIn("\t[Abstract]\n\tpublic native class Texture2D extends GodotObject {", self.text)
        self.assertIn("\t[Abstract]\n\tpublic native class Input extends GodotObject {", self.text)
        self.assertIn("\t[Abstract]\n\tpublic final native class Key {", self.text)
        self.assertNotIn("\t[Abstract]\n\tpublic native class Node ", self.text)

    def test_an_integer_the_description_gives_no_width_is_an_int64(self):
        # A global function, a built-in type's method, and a signal's argument.
        self.assertIn("\tpublic native function instanceFromID(instanceID:Int64):GodotObject;\n", self.text)
        self.assertIn("\tpublic native function randi():Int64;\n", self.text)
        self.assertIn("public native function getObjectID():Int64;", self.members("Signal"))
        self.assertIn("public native function size():Int64;", self.members("GodotArray"))

    def test_to_string_is_as3s(self):
        # AS3's Object already has toString(). A script overrides that, and the binding answers Godot's
        # _to_string with it, so neither of Godot's two methods is declared.
        self.assertFalse([line for line in self.members("GodotObject") if "toString" in line])

    def tags(self, class_name):
        """The tag lines written before a class."""
        lines = self.text.split("\n")
        at = [i for i, line in enumerate(lines) if (" class %s " % class_name) in line + " "][0]
        result = []
        while lines[at - 1].startswith("\t["):
            at -= 1
            result.insert(0, lines[at].strip())
        return result

    def test_operators(self):
        tags = self.tags("Vector2")
        self.assertIn('[Operator(op="+", right="Vector2", returns="Vector2")]', tags)
        self.assertIn('[Operator(op="*", right="Number", returns="Vector2")]', tags)
        self.assertIn('[Operator(op="*", right="int", returns="Vector2")]', tags)
        self.assertIn('[Operator(op="<", right="Vector2", returns="Boolean")]', tags)
        self.assertIn('[Operator(op="-", returns="Vector2")]', tags)

    def test_operators_with_a_number_on_the_left(self):
        tags = self.tags("Vector2")
        self.assertIn('[Operator(op="*", left="Number", returns="Vector2")]', tags)
        self.assertIn('[Operator(op="*", left="int", returns="Vector2")]', tags)

    def test_operators_as3_does_not_have_are_left_out(self):
        operators = [line for line in self.text.split("\n") if "[Operator(" in line]
        self.assertEqual(len(operators), 7)
        for line in operators:
            self.assertNotIn('"=="', line)
            self.assertNotIn('"in"', line)
            self.assertNotIn('"**"', line)
            self.assertNotIn('"&"', line)
            self.assertNotIn("String", line)
            self.assertNotIn("Function", line)

    def test_indexing(self):
        self.assertIn('[Indexed(element="Number")]', self.tags("Vector2"))
        self.assertIn('[Indexed(element="*")]', self.tags("GodotArray"))
        self.assertIn('[Indexed(element="*", keyed="true")]', self.tags("GodotDictionary"))
        self.assertEqual([tag for tag in self.tags("Signal") if "Indexed" in tag], [])

    def test_every_value_type_says_its_operators_are_all_listed(self):
        for name in ("Vector2", "Signal", "GodotArray", "GodotDictionary"):
            self.assertIn("[Operators]", self.tags(name))
        self.assertNotIn("[Operators]", self.tags("Node"))

    def test_indexing_a_wide_integer(self):
        self.assertIn('[Indexed(element="Int64")]', self.tags("PackedInt64Array"))

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

    def test_flags_of_one_name_share_the_widest_width(self):
        # Mesh.ArrayFormat holds RenderingServer.ArrayFormat's flags, whose version bits are above 32:
        # every flag enum of that name is as wide as the widest.
        api = fixture()
        node = self.node(api)
        node["enums"].append({"name": "Format", "is_bitfield": True, "values": [{"name": "FORMAT_A", "value": 1}]})
        control = [entry for entry in api["classes"] if entry["name"] == "Control"][0]
        control.setdefault("enums", []).append(
            {"name": "Format", "is_bitfield": True, "values": [{"name": "FORMAT_HIGH", "value": 2 ** 35}]})
        node["methods"].append({"name": "set_format", "is_const": False, "is_vararg": False, "is_static": False,
                                "is_virtual": False, "hash": 1,
                                "arguments": [{"name": "format", "type": "bitfield::Node.Format"}]})
        text = generate.generate(api)
        self.assertIn("public final native function setFormat(format:Int64):void;", lines_of(text, "Node"))

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

    def test_unknown_integer_width(self):
        api = fixture()
        self.node(api)["methods"][11]["return_value"]["meta"] = "int128"
        self.refuses(api, "unknown width 'int128' in Node.get_index")

    def test_names_that_are_not_identifiers(self):
        for change, message in (
                (lambda node: node["constants"].append({"name": "2D_MODE", "value": 1}), "Node: '2D_MODE' is not a name"),
                (lambda node: node["enums"][0]["values"].append({"name": "A-B", "value": 9}), "Node: 'A-B' is not a name"),
                (lambda node: node["methods"].append(dict(node["methods"][10], name="a-b")), "Node: 'a-b' is not a name"),
                (lambda node: node["signals"].append({"name": "3d_ready"}), "Node: '3dReady' is not a name"),
                (lambda node: node["constants"].append({"name": "new", "value": 1}), "Node: 'new' is not a name")):
            api = fixture()
            change(self.node(api))
            self.refuses(api, message)

    def test_global_enum_value_that_is_not_a_name(self):
        api = fixture()
        api["global_enums"][0]["values"].append({"name": "SIDE_new", "value": 9})
        self.refuses(api, "Side: 'new' is not a name")

    def test_constant_that_is_not_an_integer(self):
        api = fixture()
        self.node(api)["constants"].append({"name": "HALF", "value": 0.5})
        self.refuses(api, "Node.HALF: the value 0.5 is not an integer")

    def test_pointer_where_nothing_can_be_left_out(self):
        api = fixture()
        api["utility_functions"][0]["arguments"][0]["type"] = "const void*"
        self.refuses(api, "raw pointer type in print")

    def test_subclass_describing_a_base_method_again(self):
        api = fixture()
        control = [c for c in api["classes"] if c["name"] == "Control"][0]
        control["methods"].append(copy.deepcopy(self.node(api)["methods"][5]))
        self.refuses(api, "Control: 'getNode' names both")

    def test_object_method_the_generator_adds_is_described(self):
        api = fixture()
        api["classes"][0]["methods"].append({"name": "free", "is_const": False, "is_vararg": False,
                                             "is_static": False, "is_virtual": False, "hash": 1})
        self.refuses(api, "Object: 'free' names both")

    def test_unknown_operator(self):
        api = fixture()
        [c for c in api["builtin_classes"] if c["name"] == "Vector2"][0]["operators"].append(
            {"name": "<=>", "right_type": "Vector2", "return_type": "int"})
        self.refuses(api, "Vector2: unknown operator '<=>'")

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
			(getNode("Hitbox") as Node).childEnteredTree.add(onEntered); died.addOnce(onEntered, true); died.remove(onEntered); died.removeAll(); var listeners:int = died.numListeners; var byName:String = "died"; this[byName].add(onEntered);
			processMode = Node.PROCESS_MODE_ALWAYS;
			var at:Vector2 = new Vector2(1, 2);
			at.x = Vector2.ZERO.distanceTo(new Vector2());
			var id:Number = getInstanceID();
			if (Input.isActionPressed("jump") && Key.ESCAPE != Key.KEY_0) {
				print("jump", degToRad(90), anchorLeft, size.y, name);
			}
			addChild(new Node(), true);
			Input.mouseMode = 1;
			Input.joyConnectionChanged.add(onEntered);
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

    def test_print_takes_any_number_of_arguments(self):
        # Godot describes print(arg1, ...) though print() is allowed: a lone leading argument of a function
        # that takes any number is not required.
        self.assertIn("\tpublic native function print(...rest):void;", self.text)
        # But str() with nothing is refused by Godot: only the functions that return nothing take none.
        self.assertIn("\tpublic native function str(arg1:*, ...rest):String;", self.text)
        self.assertIn("\tpublic native function pushError(...rest):void;", self.text)
        self.assertNotIn("function print(arg1", self.text)
        # Two leading arguments are a real minimum.
        self.assertIn("\tpublic native function max(arg1:*, arg2:*, ...rest):*;", self.text)

    def test_the_committed_names_are_current(self):
        self.assertTrue(self.generator.names_text() == read(os.path.join(HERE, "godot_names.txt")),
                        "godot_names.txt is stale: run generate.py again")

    def test_godot_names(self):
        # The Godot spelling of everything declared, for the bindings: never worked out backwards from the
        # AS3 name, which loses information (getNode3d).
        names = dict(line.split("=") for line in self.generator.names_text().split("\n") if line)
        self.assertEqual(names["Input.singleton"], "@singleton")
        # A global function's signature, for the entry that takes exact types.
        self.assertEqual(names["@global.lerpf"], "lerpf 998901048 float float,float,float")
        self.assertEqual(names["@global.randf"], "randf 2086227845 float -")
        self.assertEqual(names["@global.print"], "print 2648703342 void - varargs")
        self.assertEqual(names["@global.degToRad"].split(" ")[0], "deg_to_rad")
        self.assertNotIn("@global.typeof", names)
        # A default AS3 can't write is declared `= null`; Godot's own text for it is kept for the binding.
        self.assertEqual(names["@default.Color.clamp.0"], "Color(0, 0, 0, 0)")
        self.assertEqual(names["@default.Color.clamp.1"], "Color(1, 1, 1, 1)")
        self.assertNotIn("@default.Node.add_child.1", names)
        defaults = {value for key, value in names.items() if key.startswith("@default.")}
        self.assertIn("Transform2D()", defaults)
        self.assertNotIn("Transform2D(1, 0, 0, 1, 0, 0)", defaults)
        self.assertFalse([value for value in defaults if value.startswith("Array[")])
        self.assertEqual(names["CharacterBody2D.moveAndSlide"], "move_and_slide")
        self.assertEqual(names["Node._physicsProcess"], "_physics_process")
        self.assertEqual(names["Node2D.position"], "position")
        self.assertEqual(names["CharacterBody2D.velocity"], "velocity")
        self.assertEqual(names["Node.childEnteredTree"], "child_entered_tree")
        self.assertEqual(names["GodotObject"], "Object")
        self.assertEqual(names["GodotObject.getInstanceID"], "get_instance_id")
        self.assertEqual(names["Vector2.distanceTo"], "distance_to")
        self.assertEqual(names["Vector2.x"], "x")
        self.assertEqual(names["Input.isActionPressed"], "is_action_pressed")
        self.assertEqual(names["Node"], "Node")
        self.assertGreater(len(names), 15000)
        # Every member line of a Godot class in godot.as has its name here, except AS3's own additions.
        own = {"Signal.add", "Signal.addOnce", "Signal.remove", "Signal.removeAll", "Signal.has", "Signal.numListeners"}
        missing = []
        klass = None
        import re
        for line in self.text.split("\n"):
            head = re.match(r"\tpublic (?:final )?native class (\w+)", line)
            if head:
                klass = head.group(1)
            member = re.match(r"\t\tpublic (?:static |final )*(?:native function (?:get |set )?|var )(\w+)", line)
            if klass and member and member.group(1) != klass:
                key = "%s.%s" % (klass, member.group(1))
                if key not in names and key not in own:
                    missing.append(key)
        self.assertEqual(missing[:10], [])

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
        self.assertEqual(set(re.findall(r"function get (\w+_)\(", self.text)), {"function_"})

    def test_signals_are_listened_to_through_their_property(self):
        # The string form (`connect("timeout", f)`) is not declared: `timer.timeout.add(f)` is the one way.
        members = lines_of(self.text, "GodotObject")
        self.assertEqual([line for line in members if " connect(" in line or " disconnect(" in line or
                          " isConnected(" in line], [])
        self.assertIn("public final native function emitSignal(signal:String, ...rest):int;", members)
        signal = lines_of(self.text, "Signal")
        self.assertIn("public native function add(listener:Function, deferred:Boolean = false):void;", signal)
        # numListeners stands for Godot's two ways of asking.
        self.assertEqual([line for line in signal if "Connections" in line], [])

    def test_every_hand_kept_entry_is_used(self):
        self.assertEqual(self.generator.unused_entries(), [])

    def test_no_table_of_widths(self):
        self.assertFalse(hasattr(generate, "WIDTHS"))

    def test_a_type_that_stores_32_bit_integers_keeps_int(self):
        # Godot cuts a wide value off inside these without an error, so a wide one must not reach it.
        for name in ("Vector2i", "Vector3i", "Vector4i", "Rect2i", "PackedInt32Array"):
            members = " ".join(lines_of(self.text, name))
            self.assertNotIn(":Int64", members, name)
        self.assertIn("public var x:int;", lines_of(self.text, "Vector2i"))
        self.assertIn('\t[Indexed(element="int")]\n\tpublic final native class PackedInt32Array', self.text)
        self.assertEqual(sorted(self.generator.narrow), ["PackedInt32Array", "Rect2i", "Vector2i", "Vector3i", "Vector4i"])
        self.assertIn("public native function decodeS64(byteOffset:Int64):Int64;", lines_of(self.text, "PackedByteArray"))

    def test_wide_integers_of_the_real_api(self):
        self.assertIn("public native function append(value:Int64):Boolean;", lines_of(self.text, "PackedInt64Array"))
        self.assertIn("public native function toRgba64():Int64;", lines_of(self.text, "Color"))
        self.assertIn("public native function toRgba32():Int64;", lines_of(self.text, "Color"))
        self.assertIn("public native function getId():Int64;", lines_of(self.text, "RID"))
        self.assertIn("\tpublic native function randi():Int64;\n", self.text)
        self.assertIn("\tpublic native function clampi(value:Int64, min:Int64, max:Int64):Int64;\n", self.text)
        self.assertNotIn(":Number = 34359738368", self.text)

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
        for line in ("public final native function moveAndSlide():Boolean;",
                     "public native function _physicsProcess(delta:Number):void;",
                     "public final native function get timeout():Signal;",
                     "public static const ESCAPE:int = 4194305;",
                     "public static const PROCESS_MODE_PAUSABLE:int = 1;",
                     "public static native function isActionPressed(action:String, exactMatch:Boolean = false)"
                     ":Boolean;",
                     "public final native function get visible():Boolean;",
                     "public final native function isOnFloor():Boolean;"):
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
