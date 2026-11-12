package {
	import godot.*;
	public class Base extends CharacterBody2D {
		public var steps:int = 0;
		override public function _physicsProcess(delta:Number):void { steps++; }
	}
}
