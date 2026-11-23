package {
	import godot.*;

	// A script Godot reaches from its worker threads (threads.gd): AS3 runs on the main thread only.
	public class Threaded extends RefCounted {
		public static var made:int;
		public var partner:Counted;

		public function Threaded() {
			made++;
			partner = new Counted();
		}
		public var calls:int;
		public function namedRead(source:*):int { calls++; return source.calls; }
		public function partnerID():Int64 { return partner.getInstanceID(); }
		public function madeCount():int { return made; }
		public function toString():String { return "threaded"; }
	}
}
