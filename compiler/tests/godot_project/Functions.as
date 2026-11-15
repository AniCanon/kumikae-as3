package {
	import godot.*;

	// Godot's global functions: functions.gd works the same thing out in GDScript.
	public class Functions extends Node2D {
		public function mix(a:Number, b:Number, t:Number):Number { return lerpf(a, b, t); }
		// A rest parameter, given its arguments by Godot.
		public function gather(first:int, ...more):String { return first + ";" + more.length + ";" + more.join(","); }
		public function hold(v:Number):Number { return clampf(v, -1, 1); }
		public function holdWhole(v:Int64):Int64 { return clampi(v, 0, 10); }
		public function size(v:Int64):Int64 { return absi(v); }
		public function larger(a:int, b:int):int { return maxi(a, b); }
		public function wave(angle:Number):Number { return sin(degToRad(angle)); }
		public function snap(v:Number):Number { return snappedf(v, 0.25); }
		public function wrap(v:int):int { return wrapi(v, 0, 5); }
		public function remainder(a:int, b:int):int { return posmod(a, b); }
		public function close(a:Number, b:Number):Boolean { return isEqualApprox(a, b); }
		public function chance():Boolean { var r:Number = randf(); return r >= 0 && r < 1; }
		public function roll():Boolean { var r:int = randiRange(1, 6); return r >= 1 && r <= 6; }
		public function whole(v:Number):int { return floori(v) + roundi(v); }
		public function mark(text:String):Int64 { return hash(text); }
		public function join(n:int, text:String, v:Vector2):String { return str(n, text, v); }
		public function alive(node:Node):Boolean { return isInstanceValid(node); }
		public function find(id:Int64):GodotObject { return instanceFromID(id); }
		public function say(n:int, v:Vector2):void { print("said ", n, " ", v); printerr("to the error output"); pushWarning("a warning from AS3"); }
		// A function that gives back anything, where the program says what it expects.
		public function mixAny(a:Number, b:Number, t:Number):Number { var x:Number = lerp(a, b, t); return x; }
		public function largest(a:int, b:int, c:int):int { var n:int = max(a, b, c); return n; }
		// What a function that gives back anything gives is converted where it is used, as AS3 converts.
		public function lerpVector():Vector2 { var v:Vector2 = lerp(Vector2.ZERO, Vector2.ONE, 0.5); return v; }
		public function lerpCut():int { return int(lerp(1, 2, 0.75)); }
		public function maxAsCondition():Boolean { return Boolean(max(0, 1)); }
		public function lerpText():String { return "" + lerp(1, 2, 0.5) + " " + max(1, 5); }
		public function lerpAnything(a:*, b:*):* { return lerp(a, b, 0.5); }
		public function lerpWrongType():Vector2 { var v:Vector2 = lerp(1, 2, 0.5); return v; }
		// A wide whole number into a Number from a function that gives back anything: the RangeError its
		// typed twin gives.
		public function widest(a:Int64, b:Int64):Number { var n:Number = max(a, b); return n; }
		public function wrong(text:String):Number { var x:Number = lerp(text, 2, 0.5); return x; }
		public function count(n:int):int { var total:int = 0; for (var i:int = 0; i < n; i++) { total += maxi(i, 3); } return total; }
		// Review of part C.
		public function nothing():Boolean { print(); printerr(); pushWarning(); return true; }
		public function wholes(i:int):String { return str(-5, " ", 5, " ", -5 + 2, " ", 0x10, " ", 2 * 3, " ", i * 2, " ", 1.5, " ", i / 2); }
		public function noAnswer():String { var s:String = max("a", "b"); return s; }
		public function shown(a:Number, b:Number, t:Number):String { var s:String = lerp(a, b, t); return s; }
		public var pings:int = 0;
		public function ping():void { pings++; }
		public function run():int { var a:PackedStringArray = new PackedStringArray(); a.append("hello"); return OS.execute("echo", a, null, true); }
		public function reached():int { pings = 0; propagateCall("ping", null, true); return pings; }
	}
}
