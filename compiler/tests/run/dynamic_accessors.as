package { public class dynamic_accessors {
	public function dynamic_accessors() {
		var u:* = new Thing();
		trace(u.size, u["size"]);
		u.size = 7;
		trace(u.size, u.plain);
		trace(u.describe());
		var s:IShape = new Thing();
		var any:* = s;
		trace(any.size, any.area());
		var o:Object = {thing: new Thing()};
		trace(o.thing.size);
		try { u.hidden = 1; } catch (e:ReferenceError) { trace("ReferenceError"); }
		try { u.ro = 1; } catch (e:ReferenceError) { trace(e.message); }
		try { trace(u.wo); } catch (e:ReferenceError) { trace(e.message); }
		u.wo = 5;
		trace(u.size);
	}
} }
interface IShape { function area():Number; }
class Thing implements IShape {
	private var n:int = 3;
	public var plain:String = "p";
	public function get size():int { return n; }
	public function set size(v:int):void { n = v * 2; }
	public function describe():String { return "thing " + n; }
	public function area():Number { return n * n; }
	private function get hidden():int { return 0; }
	public function get ro():int { return 1; }
	public function set wo(v:int):void { n = v; }
}
