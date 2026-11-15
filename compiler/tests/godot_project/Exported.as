package {
	import godot.*;

	// [Export]: Godot's inspector shows these fields, and exported.tscn saves values for some of them.
	public class Exported extends Node2D {
		[Export] public var speed:Number = 5;
		[Export] public var lives:int = 3;
		[Export] public var title:String = "hero";
		[Export] public var alive:Boolean = true;
		[Export] public var spot:Vector2;
		[Export] public var items:Array;
		[Export] public var below:Number = -1.5;
		[Export] public var made:Vector2 = new Vector2(1, 2);
		[Export] public var offset:Vector2i = new Vector2i(-1, 3);
		[Export] public var tint:Color = new Color(0.5, -0, 1);
		[Export] public var texture:Texture2D;
		[Export] public var target:Node;
		public var notExported:int = 4;

		// In the editor no AS3 runs: an object there never gets this name.
		public function Exported() { name = "made by AS3"; }

		public function describe():String {
			return [speed, lives, title, alive, spot, items ? items.join("+") : "none", below, made, texture ? "texture " + texture.getWidth() + "x" + texture.getHeight() : "no texture",
				target ? String(target.name) : "no target"].join(" ");
		}
	}
}
