package { public class array_loops {
	public function array_loops() {
		var keys:String = "";
		for (var k:* in ["a", "b"]) { keys += typeof k + ":" + k + " "; }
		trace(keys);
		var sparse:Array = new Array(3);
		sparse[1] = "x";
		var seen:int = 0;
		for (var s:String in sparse) { seen++; trace(s, sparse[s]); }
		for each (var v:* in sparse) { trace(v); }
		trace(seen);
		var total:int = 0;
		for (var i:int in [5, 6, 7]) { total += i; }
		trace(total);
		var vk:String = "";
		for (var n:* in new <int>[4, 5]) { vk += typeof n + " "; }
		trace(vk);
	}
} }
