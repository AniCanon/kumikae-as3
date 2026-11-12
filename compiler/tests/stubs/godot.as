// A hand-written stand-in for the Godot classes that the corpus and the golden tests use. Stage 2 replaces
// it with declarations generated from Godot's own API description. Signatures here are approximate; every
// member is one that a test file names.
package godot {
	public native class Node {
		public native function _ready():void;
		public native function _process(delta:Number):void;
		public native function _physics_process(delta:Number):void;
		public native function add_child(node:Node):void;
		public native function connect(signal:String, callable:Function):int;
		public native function emit_signal(signal:String, ...rest):int;
		public native function set_meta(name:String, value:*):void;
		public native function get_meta(name:String):*;
	}
	public native class Node2D extends Node {
		public native function get position():Vector2;
		public native function set position(value:Vector2):void;
		public native function get rotation():Number;
		public native function set rotation(value:Number):void;
		public native function get_world_2d():Object;
	}
	public native class Line2D extends Node2D {
		public var points:Object;
		public native function get_point_count():int;
	}
	public native class StaticBody2D extends Node2D {}
	public native class CollisionShape2D extends Node2D {
		public var shape:Object;
	}
	public native class CircleShape2D {
		public var radius:Number;
	}
	public native class PhysicsDirectSpaceState2D {
		public native function intersect_ray(query:PhysicsRayQueryParameters2D):Dictionary;
	}
	public native class PhysicsRayQueryParameters2D {
		public var from:Vector2;
		public var to:Vector2;
	}
	public native class Dictionary {
		public native function is_empty():Boolean;
	}
	public final native class Vector2 {
		public var x:Number;
		public var y:Number;
	}
}
