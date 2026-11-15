package {
	import godot.*;

	// Godot's own classes with `is` and `as`: events.gd does the same in GDScript.
	public class Events extends Node2D {
		public function kind(event:InputEvent):String {
			if (event is InputEventScreenDrag) {
				return "drag";
			}
			if (event is InputEventScreenTouch) {
				return "touch";
			}
			if (event is InputEventMouse) {
				return "mouse";
			}
			return event is InputEvent ? "other" : "none";
		}
		public function where(event:InputEvent):Vector2 {
			var touch:InputEventScreenTouch = event as InputEventScreenTouch;
			if (touch != null) {
				return touch.position;
			}
			var drag:InputEventScreenDrag = event as InputEventScreenDrag;
			return drag == null ? new Vector2(-1, -1) : drag.position + drag.relative;
		}
		public function isNode(thing:GodotObject):Boolean { return thing is Node; }
		public function asSprite(thing:Node):String {
			var sprite:Sprite2D = thing as Sprite2D;
			return sprite == null ? "not a sprite" : sprite.name;
		}
		public function nothing():String {
			var none:InputEvent = null;
			return "" + (none is InputEvent) + (none as InputEventKey == null);
		}
		// Touches that reach a script as Godot delivers them.
		private var seen:String = "";
		override public function _input(event:InputEvent):void {
			var touch:InputEventScreenTouch = event as InputEventScreenTouch;
			if (touch != null) {
				seen += (touch.pressed ? "down" : "up") + touch.position.x + " ";
			}
			if (event is InputEventScreenDrag) {
				seen += "drag ";
			}
		}
		public function heard():String { return seen; }
	}
}
