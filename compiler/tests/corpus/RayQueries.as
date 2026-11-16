package {
	import godot.CircleShape2D;
	import godot.CollisionShape2D;
	import godot.GodotDictionary;
	import godot.Node2D;
	import godot.PhysicsDirectSpaceState2D;
	import godot.PhysicsRayQueryParameters2D;
	import godot.StaticBody2D;
	import godot.Vector2;

	// Version 2: the query result stays the engine's Dictionary (redesign section 7).
	public class RayQueries extends Node2D {
		private static const RAYS:int = 2000;

		private var params:PhysicsRayQueryParameters2D;
		private var hits:int = 0;
		private var sumX:Number = 0;

		override public function _ready():void {
			for (var gy:int = 0; gy < 8; gy++) {
				for (var gx:int = 0; gx < 8; gx++) {
					var body:StaticBody2D = new StaticBody2D();
					var shape:CollisionShape2D = new CollisionShape2D();
					var circle:CircleShape2D = new CircleShape2D();
					circle.radius = 20;
					shape.shape = circle;
					body.addChild(shape);
					body.position = new Vector2(gx * 100 + 50, gy * 100 + 50);
					addChild(body);
				}
			}
			params = new PhysicsRayQueryParameters2D();
		}

		override public function _physicsProcess(delta:Number):void {
			var space:PhysicsDirectSpaceState2D = getWorld2D().directSpaceState;
			for (var i:int = 0; i < RAYS; i++) {
				params.from = new Vector2(-50, i % 800);
				params.to = new Vector2(850, (i * 7) % 800);
				var result:GodotDictionary = space.intersectRay(params);
				if (!result.isEmpty()) {
					hits++;
					var pos:Vector2 = result["position"];
					sumX += pos.x;
				}
			}
		}

		public function checksum():Number {
			return hits + sumX;
		}
	}
}
