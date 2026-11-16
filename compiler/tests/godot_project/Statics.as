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
		// An error whose message looks like a stack line is still shown at the line it was thrown from.
		public function fakeTrace():void { throw new Error("odd\n\tat fake()[nowhere.as:99]"); }
		// A class's static method, called through its own entry: a call Godot refused would be the script's error.
		public function staticWorks():String { return GodotJSON.stringify(3); }
		// Its arguments are worked out left to right, and what it gives back is checked where it is stored.
		private var order:String = "";
		private function noted(p_text:String):String { order += p_text; return p_text; }
		public function staticOrder():String { GodotJSON.stringify(noted("a"), noted("b")); return order; }
		// A Godot property written through a missing script object: the value first, then AS3's null error.
		public function writeMissing():String {
			order = "";
			var none:Statics = null;
			try { none.visible = noted("value") == "value"; } catch (e:TypeError) { return order + " caught"; }
			return order;
		}
		public function staticWrongType():String {
			try { var node:Node = GodotJSON.parseString("[1]"); } catch (e:TypeError) { return "caught"; }
			return "carried on";
		}
		public function orphansShown():Boolean { Node.printOrphanNodes(); return true; }
		// A variable that holds a singleton reaches its members, on that object.
		public function viaObject():Boolean {
			var i:Input = Input.singleton;
			return i.isActionPressed("ui_accept") == Input.isActionPressed("ui_accept") && i.mouseMode == Input.mouseMode;
		}
		public function viaNull():Boolean {
			var none:Input = null;
			try { none.isActionPressed("ui_accept"); } catch (e:TypeError) { return true; }
			return false;
		}
		// An editor-only singleton in a game: one AS3 error, which can be caught, and nothing from Godot.
		public function editorOnly():Boolean {
			try { var editor:EditorInterface = EditorInterface.singleton; } catch (e:Error) { return true; }
			return false;
		}
		public function slow(scale:Number):void { Engine.timeScale = scale; }
		public function timeScale():Number { return Engine.timeScale; }
		public function faster():Number { Engine.timeScale *= 2; return Engine.timeScale; }
		public function atStart():int { return startTicks; }
		public function mouse():int { return Input.mouseMode; }
		public function listen():void { Input.joyConnectionChanged.add(onJoy); }
		public function unlisten():void { Input.joyConnectionChanged.remove(onJoy); }
		public function clear():void { Input.joyConnectionChanged.removeAll(); }
		public function listeners():int { return Input.joyConnectionChanged.numListeners; }
		private static function clearStatic():void { Input.joyConnectionChanged.removeAll(); }
		public function clearFromStatic():void { clearStatic(); }
		public function listenStatic():void { Input.joyConnectionChanged.add(onJoyStatic); }
		public function clearInPlace():void { var clearing:Function = function ():void { Input.joyConnectionChanged.removeAll(); }; clearing(); }
		public function listening():Boolean { return Input.joyConnectionChanged.has(onJoy); }
		private function onJoy(device:int, connected:Boolean):void { joys += connected ? device : 100; }
		// Listeners that are not methods: a function written in place, which captures what it uses, and a
		// static method. One kept in a variable is removed through it.
		public static var staticJoys:int = 0;
		private static function onJoyStatic(device:int, connected:Boolean):void { staticJoys += device; }
		private var inPlace:Function;
		public function listenInPlace():void {
			var self:Statics = this;
			inPlace = function (device:int, connected:Boolean):void { self.joys += device * 10; };
			Input.joyConnectionChanged.add(inPlace);
			Input.joyConnectionChanged.add(onJoyStatic);
		}
		public function unlistenInPlace():void {
			Input.joyConnectionChanged.remove(inPlace);
			Input.joyConnectionChanged.remove(onJoyStatic);
		}
		public function heardStatic():int { return staticJoys; }
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
		public function hsv(h:Number, s:Number, v:Number):Color { return Color.fromHSV(h, s, v); }
		public function html(text:String):Color { return Color.html(text); }
		public function euler(v:Vector3):Quaternion { return Quaternion.fromEuler(v); }
		public function image():int {
			var made:Image = Image.createEmpty(4, 2, false, Image.FORMAT_RGBA8);
			return made.getWidth() * 10 + made.getHeight();
		}
		public function orphans():void { Node.printOrphanNodes(); }

		// Godot calling a method from one of its worker threads is refused: AS3 runs on the main thread.
		private var total:int = 0;
		public var shared:int = 1;
		private var text:String = "";
		public function race():int {
			for (var i:int = 0; i < 64; i++) { WorkerThreadPool.addTask(spin); }
			for (var j:int = 0; j < 200000; j++) { text = "m" + j; total++; }
			return total;
		}
		private function spin():void { for (var k:int = 0; k < 20000; k++) { text = "w" + k; total++; } }

		// A Godot value with parts starts at its zero wherever it is read.
		public function twice(first:Boolean):Vector2 { if (first) { var v:Vector2 = Vector2.ONE; } else { var v:Vector2; } return v; }
		public function twiceBack(first:Boolean):Vector2 { if (first) { var v:Vector2; } else { var v:Vector2 = Vector2.ONE; } return v; }
		public function skipped(run:Boolean):Vector2 { if (run) { var v:Vector2 = Vector2.ONE; } return v; }
		private var early:Vector2 = late;
		private var late:Vector2;
		private static var earlyStatic:Vector2 = lateStatic;
		private static var lateStatic:Vector2;
		public function earlyField():Vector2 { return early; }
		public function earlyStaticField():Vector2 { return earlyStatic; }

		// A Godot value with parts starts at its zero; anything else starts missing.
		public function unset():Vector2 { var v:Vector2; return v; }
		public function unsetTint():Color { return tint; }
		public function unsetSpot():String { return "" + spot; }
		public function unsetList():Boolean { return list == null; }
		public function cleared():Boolean { var v:Vector2 = Vector2.ONE; v = null; return v == null; }
	}
}
