package tools {
	import flash.utils.Dictionary;
	import flash.utils.getTimer;

	public function Qualified():Object {
		var cache:Dictionary = new Dictionary();
		var also:flash.utils.Dictionary = new flash.utils.Dictionary(true);
		var node:godot.Node = new godot.Node();
		var started:int = getTimer() + flash.utils.getTimer();
		var list:Vector.<godot.Node> = new <godot.Node>[node];
		var failure:Error = new RangeError("out of range");
		return { cache: cache, also: also, started: started, list: list, failure: failure };
	}
}
