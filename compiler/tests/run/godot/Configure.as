package {
	import godot.*;

	public class Configure extends Node {
		public function attach():void {
			var timer:* = new Timer();
			timer.waitTime = 2.5;
			timer.oneShot = true;
			timer.autostart = true;
			var wait:Number = timer.waitTime;
			addChild(timer);
			timer.waitTime = wait;
		}
	}
}
