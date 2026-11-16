package {
	import godot.*;

	// A script whose base is Godot's Object, which GDScript attaches to a RefCounted (cycles.gd): its object
	// and the RefCounted must live while either is held, by AS3 or by Godot, as a script on a Resource does.
	public class Unjoined extends GodotObject {
		public static var held:Unjoined;
		public var label:String;
		public var partner:UnjoinedPartner;

		// A loop with a plain object, which holds this object's Godot object too when p_through.
		public function loop(p_label:String, p_through:Boolean):void {
			label = p_label;
			partner = new UnjoinedPartner();
			partner.back = this;
			if (p_through) {
				partner.res = this;
			}
		}
		public function word():String { return label + " " + (partner ? partner.word + " " + (partner.back == this) : "no partner"); }
		public function hold():void { held = this; }
	}
}
