package {
	import godot.*;

	// Godot's global functions: functions.gd works the same thing out in GDScript.
	public class Functions extends Node2D {
		public function mix(a:Number, b:Number, t:Number):Number { return lerpf(a, b, t); }
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
		public function find(id:Int64):GodotObject { return instanceFromId(id); }
		public function say(n:int, v:Vector2):void { print("said ", n, " ", v); printerr("to the error output"); pushWarning("a warning from AS3"); }
		// A function that gives back anything, where the program says what it expects.
		public function mixAny(a:Number, b:Number, t:Number):Number { var x:Number = lerp(a, b, t); return x; }
		public function largest(a:int, b:int, c:int):int { var n:int = max(a, b, c); return n; }
		public function wrong(text:String):Number { var x:Number = lerp(text, 2, 0.5); return x; }
		public function count(n:int):int { var total:int = 0; for (var i:int = 0; i < n; i++) { total += maxi(i, 3); } return total; }
	}
}
