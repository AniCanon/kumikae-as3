package {
	import godot.*;

	// Singletons, constants and static methods: statics.gd works the same thing out in GDScript.
	public class Statics extends Node2D {
		private static var startTicks:int = Engine.physicsTicksPerSecond;
		public var joys:int = 0;
		private var tint:Color;
		private var spot:Vector2;
		private var list:GodotArray;

		// Singletons.
		public function pressed(action:String):Boolean { return Input.isActionPressed(action); }
		public function strength(action:String):Number { return Input.getActionStrength(action); }
		public function stick():Vector2 { return Input.getVector("ui_left", "ui_right", "ui_up", "ui_down"); }
		public function system():String { return OS.getName(); }
		public function slow(scale:Number):void { Engine.timeScale = scale; }
		public function timeScale():Number { return Engine.timeScale; }
		public function faster():Number { Engine.timeScale *= 2; return Engine.timeScale; }
		public function atStart():int { return startTicks; }
		public function mouse():int { return Input.mouseMode; }
		public function listen():void { Input.joyConnectionChanged.add(onJoy); }
		public function unlisten():void { Input.joyConnectionChanged.remove(onJoy); }
		public function clear():void { Input.joyConnectionChanged.removeAll(); }
		public function listening():Boolean { return Input.joyConnectionChanged.has(onJoy); }
		private function onJoy(device:int, connected:Boolean):void { joys += connected ? device : 100; }
		public function inputClass():String { return Input.singleton.getClass(); }
		public function sameSingleton():Boolean { return Input.singleton == Input.singleton; }
		public function singletonObject():GodotObject { return Input.singleton; }

		// Constants.
		public function escape():int { return Key.ESCAPE; }
		public function captured():int { return Input.MOUSE_MODE_CAPTURED; }
		public function readyNotice():int { return Node.NOTIFICATION_READY; }
		public function axis():int { return Vector2.AXIS_Y; }
		public function zero():Vector2 { return Vector2.ZERO; }
		public function up():Vector2 { return Vector2.UP; }
		public function red():Color { return Color.RED; }
		public function forward():Vector3 { return Vector3.FORWARD; }
		public function identity():Transform2D { return Transform2D.IDENTITY; }
		public function sum():Vector2 { return Vector2.UP * 2 + Vector2.ONE; }
		public function upX():Number { return Vector2.UP.y; }
		public function keyOr(key:int = Key.ESCAPE):int { return key; }

		// Static methods.
		public function fromAngle(angle:Number):Vector2 { return Vector2.fromAngle(angle); }
		public function hsv(h:Number, s:Number, v:Number):Color { return Color.fromHsv(h, s, v); }
		public function html(text:String):Color { return Color.html(text); }
		public function euler(v:Vector3):Quaternion { return Quaternion.fromEuler(v); }
		public function image():int {
			var made:Image = Image.createEmpty(4, 2, false, Image.FORMAT_RGBA8);
			return made.getWidth() * 10 + made.getHeight();
		}
		public function orphans():void { Node.printOrphanNodes(); }

		// A Godot value with parts starts at its zero; anything else starts missing.
		public function unset():Vector2 { var v:Vector2; return v; }
		public function unsetTint():Color { return tint; }
		public function unsetSpot():String { return "" + spot; }
		public function unsetList():Boolean { return list == null; }
		public function cleared():Boolean { var v:Vector2 = Vector2.ONE; v = null; return v == null; }
	}
}
