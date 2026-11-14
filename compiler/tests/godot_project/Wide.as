package {
	import godot.*;

	// Int64 at Godot's edge: wide.gd works the same thing out in GDScript.
	public class Wide extends Node2D {
		public var kept:Int64 = 9007199254740993;
		public var heard:Int64 = 0;

		public function idOf(object:GodotObject):Int64 { return object.getInstanceId(); }
		public function sameId(object:GodotObject, id:Int64):Boolean { return object.getInstanceId() == id; }
		public function rgba64(color:Color):Int64 { return color.toRgba64(); }
		public function rgba32(color:Color):uint { return color.toRgba32(); }
		public function count(items:GodotArray):int { return items.size(); }
		public function frames():Boolean { return Engine.getPhysicsFrames() >= 0 && Time.getTicksUsec() > 0; }
		public function element(wide:PackedInt64Array, at:int):Int64 { return wide[at]; }
		public function plusOne(wide:PackedInt64Array):Int64 { return wide.get(0) + 1; }
		public function stored(wide:PackedInt64Array, value:Int64):PackedInt64Array { wide.append(value); wide[0] = value - 1; return wide; }
		public function narrow(ints:PackedInt32Array, value:Int64):PackedInt32Array { ints.append(value); return ints; }
		public function echo(value:Int64):Int64 { return value; }
		public function next(value:Int64):Int64 { return value + 1; }
		public function asInt(value:int):int { return value; }
		public function asNumber(value:Number):Number { return value; }
		public function toNumber(value:Int64):Number { return value; }
		public function product(cell:Vector2i, a:int, b:int):Vector2i { return cell * (a * b); }
		public function cell(x:Int64):Vector2i { return new Vector2i(x, 0); }
		public function cell3(x:Int64):Vector3i { return new Vector3i(x, 1, 2); }
		public function box(x:Int64):Rect2i { return new Rect2i(x, x, 5, 5); }
		public function over(a:Vector2i, by:Int64):Vector2i { return a / by; }
		public function remainder(a:Vector2i, by:Int64):Vector2i { return a % by; }
		public function times(a:Vector2i, by:Int64):Vector2i { return a * by; }
		public function timesLeft(a:Vector2i, by:Int64):Vector2i { return by * a; }
		public function grow(a:Vector2i, by:Int64):Vector2i { var v:Vector2i = a; v *= by; return v; }
		public function atLeastOne(items:GodotArray):int { return items.size() || 1; }
		public function listen():void { Input.joyConnectionChanged.add(onJoy); }
		public function unlisten():void { Input.joyConnectionChanged.remove(onJoy); }
		private function onJoy(device:Int64, connected:Boolean):void { heard = device; }
		public function flags():Int64 { return RenderingServer.ARRAY_FLAG_COMPRESS_ATTRIBUTES; }
	}
}
