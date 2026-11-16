package {
	import godot.*;

	// A script on a Resource, in a loop with a plain object (cycles.gd): the cycle collector must free the
	// loop when nothing else holds either, and leave it whole while GDScript or AS3 holds it.
	public class Paired extends Resource {
		public static var held:Paired;
		public static var read:Paired;
		public static var written:Paired;
		public var label:String;
		public var partner:Partner;

		public function partnerWord():String { return partner ? partner.word + " " + (partner.back == this) : "no partner"; }
		// A getter and a setter that keep the object they run on, as GDScript reads and writes it.
		public function get keeping():String { read = this; return label; }
		public function set keeper(p_label:String):void { written = this; label = p_label; }
	}
}
