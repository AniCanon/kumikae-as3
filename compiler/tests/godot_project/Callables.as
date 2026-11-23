package {
	import godot.*;

	// Godot's methods as values. callables.gd works the same thing out in GDScript.
	public class Callables extends Node2D {
		[Signal] public var pinged:Signal;
		private var child:Node2D;
		private var other:Node2D;
		private var kept:Function;

		public function setUp(first:Node2D, second:Node2D):void { child = first; other = second; }
		private function fresh():void { pinged.removeAll(); child.show(); other.show(); show(); }

		// Where Godot takes a function: a method of another object, and one of the script's own Godot object.
		public function listens():Array {
			fresh();
			pinged.add(child.hide);
			pinged.add(hide);
			pinged.emit();
			return [child.visible, other.visible, visible, pinged.numListeners];
		}
		// Held in a variable, called from AS3, and then given to Godot: the same listener either way.
		public function held():Array {
			fresh();
			var f:Function = child.hide;
			f();
			var before:Boolean = child.visible;
			pinged.add(f);
			var has:Boolean = pinged.has(child.hide);
			pinged.remove(child.hide);
			return [before, has, pinged.has(f), pinged.numListeners];
		}
		public function through(f:Function):Boolean { pinged.add(f); return pinged.has(child.hide); }
		public function passed():Array {
			fresh();
			var had:Boolean = through(child.hide);
			pinged.removeAll();
			return [had, pinged.has(child.hide), pinged.numListeners];
		}
		// The same method of the same object is one function.
		public function same():Array {
			var f:Function = child.hide;
			var untyped:* = child.hide;
			var keys:Object = {};
			return [child.hide == child.hide, f == untyped, f === child.hide, child.hide == other.hide, child.hide == child.show, hide == this.hide,
				f != null, typeof f];
		}
		// Godot's own Callable of the object and the method, which GDScript finds equal to its own.
		public function connectHide():void { fresh(); pinged.add(child.hide); }
		// Arguments are counted and converted as a call of one of the program's functions is.
		public function counted():Array {
			var f:Function = child.hide;
			var add:Function = child.addToGroup;
			var said:Array = [];
			try { f(1); } catch (e:ArgumentError) { said.push(e.message); }
			try { add(); } catch (e:ArgumentError) { said.push(e.message); }
			try { child.isAncestorOf.call(null, "text"); } catch (e:TypeError) { said.push(e.errorID); }
			add("tagged");
			add("kept", false);
			said.push(child.isInGroup("tagged"), child.isInGroup("kept"), f.length, add.length);
			return said;
		}
		public function applied():Array {
			var inside:Function = child.isInGroup;
			return [inside.apply(null, ["tagged"]), inside.call(null, "missing")];
		}
		// A typed callback takes one whose signature is the callback's.
		public function typed():Array {
			fresh();
			var later:()=>void = child.hide;
			later();
			var asks:(String)=>Boolean = child.isInGroup;
			return [child.visible, asks("tagged")];
		}
		// What Godot's own collections call.
		public function filtered():int { return getChildren().filter(isAncestorOf).size(); }
		// A method of no object, a global function, and a method of the engine's one object of a class.
		public function statics():Array {
			var turn:Function = Vector2.fromAngle;
			var mix:Function = lerpf;
			var down:Function = Input.isActionPressed;
			var v:Vector2 = turn(0);
			return [v, mix(2, 6, 0.3), down("ui_accept"), turn == Vector2.fromAngle, mix == lerpf];
		}
		public function staticListener():Boolean { fresh(); pinged.add(Input.flushBufferedEvents); pinged.emit(); return pinged.has(Input.flushBufferedEvents); }
		// A method of one of Godot's values.
		public function ofValue():Array {
			var v:Vector2 = new Vector2(1, 2);
			var length:Function = v.length;
			var dot:Function = v.dot;
			return [length(), dot(new Vector2(0.5, 0.5)), length == v.length];
		}
		// It is released with its object: a freed object's method is no listener, and calling it says so.
		public function keep(node:Node2D):void { kept = node.hide; }
		public function afterFree():String {
			try { kept(); } catch (e:TypeError) { return "TypeError " + e.errorID; }
			return "called";
		}
		public function listenFreed():String {
			try { pinged.add(kept); } catch (e:TypeError) { return "TypeError " + e.errorID; }
			return "added";
		}
		public function drop():void { kept = null; }
	}
}
