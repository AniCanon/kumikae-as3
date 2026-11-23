package {
	import godot.*;
	import flash.utils.Dictionary;

	// Godot's classes as class values. kinds.gd works the same thing out in GDScript.
	public class Kinds extends Node2D {
		private function make(kind:Class):* { return new kind(); }
		private function isOne(value:*, kind:Class):Boolean { return value is kind; }

		// `new`, `is` and `as` with a class held in a variable.
		public function made():Array {
			var kind:Class = Sprite2D;
			var node:* = make(kind);
			var typed:Node2D = new kind();
			var said:Array = [node is kind, isOne(node, Node2D), isOne(node, Node), isOne(node, Timer), (node as kind) == node, (node as Timer) == null,
				typed is Sprite2D, isOne(null, kind), isOne(5, kind), node instanceof kind];
			(node as Node).free();
			typed.free();
			return said;
		}
		// A script object is the Godot object it is attached to.
		public function own():Array { return [isOne(this, Node2D), isOne(this, CanvasItem), isOne(this, Control), isOne(this, GodotObject)]; }
		// One of Godot's value types, made with what its constructor takes.
		public function values():Array {
			var kind:Class = Vector2;
			var color:Class = Color;
			var made:* = new kind(1.5, 2);
			return [made, new kind(), new color(1, 0.5, 0), made is kind, made is color, isOne(new Vector2(), Vector2), (made as kind) == made, (made as color) == null];
		}
		// A reference-counted object lives while the script holds it.
		public function counted():Array {
			var kind:Class = RefCounted;
			var held:* = new kind();
			var typed:RefCounted = held;
			return [held is kind, held is Resource, typed.getReferenceCount() > 0];
		}
		// A call of the class is a cast.
		public function cast(value:*):* { var kind:Class = Node2D; return kind(value); }
		public function castFails():String {
			var kind:Class = Node2D;
			try { kind(new Vector2(1, 2)); } catch (e:TypeError) { return e.message; }
			return "cast";
		}
		public function castNamedFails():String {
			var value:* = new Vector2(1, 2);
			try { Node2D(value); } catch (e:TypeError) { return e.message; }
			return "cast";
		}
		// The same class is the same value, and says its name.
		public function same():Array {
			var kind:Class = Node2D;
			var table:Dictionary = new Dictionary();
			table[Node2D] = "node";
			table[Vector2] = "vector";
			return [kind == Node2D, kind == Node, kind === Node2D, table[kind], table[Vector2], String(kind), String(GodotObject), kind is Class, typeof kind];
		}
		// A class the engine won't create.
		public function refused():Array {
			var said:Array = [];
			for each (var kind:* in [CanvasItem, Input]) {
				try { new kind(); said.push("made"); } catch (e:Error) { said.push("refused"); }
			}
			return said;
		}
	}
}
