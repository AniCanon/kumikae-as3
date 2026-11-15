package {
	import godot.*;

	public class Exports extends Node2D {
		[Export] public var speed:Number = 5;
		[Export] public var lives:int;
		[Export] public var big:Int64;
		[Export] public var title:String = "hero";
		[Export] public var alive:Boolean = true;
		[Export] public var spot:Vector2;
		[Export] public var tint:Color;
		[Export] public var points:PackedVector2Array;
		[Export] public var table:GodotDictionary;
		[Export] public var list:GodotArray;
		[Export] public var items:Array;
		[Export] public var texture:Texture2D;
		[Export] public var target:Node;

		[Export(range="0,10")] public var hinted:int;
		[Export] public static var shared:int;
		[Export] private var secret:int;
		[Export] var plain:int;
		[Export] public const FIXED:int = 3;
		[Export] public var anything:*;
		[Export] public var object:Object;
		[Export] public var numbers:Vector.<int>;
		[Export] public var mine:Exports;
		[Export] public var heard:Signal;
		[Export] [Signal] public var both:Signal;
		[Export] public var callback:Function;
		[Export] public var counted:RefCounted;

		[Export] public var made:Vector2 = new Vector2(1, -2.5);
		[Export] public var below:Number = -1;
		[Export] public var none:Node = null;
		[Export] public var computed:Number = Math.PI;
		[Export] public var product:int = 2 * 3;
		[Export] public var listed:Array = [1];
		[Export] public var fresh:Resource = new Resource();
		[Export] public var moved:Vector2 = new Vector2(speed, 1);
		[Export] public var unset:Number;
		[Export] public var onOff:Boolean = 1;
		[Export] public var worded:Boolean = "x";
		[Export] public var nothing:int = null;
		[Export] public var numbered:String = 5;
		[Export] public var flagged:Number = true;
		[Export] public var noText:String = null;
	}
}

import godot.*;

class Plain {
	[Export] public var nowhere:int;
}

class Holder extends Resource {
	[Export] public var node:Node;
}
