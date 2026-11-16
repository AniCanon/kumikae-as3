package {
	import godot.*;

	// Loops between script objects of reference-counted classes and plain ones, for cycles.gd.
	public class Cycles extends Node {
		private static function paired(p_label:String, p_loop:Boolean):Paired {
			var made:Paired = new Paired();
			made.label = p_label;
			made.partner = new Partner();
			if (p_loop) {
				made.partner.back = made;
			}
			return made;
		}
		// Held by nobody once this returns.
		public function nobody():Int64 { return paired("nobody", true).getInstanceID(); }
		// Held by GDScript alone: the plain half holds the script object, not its Resource.
		public function godotOnly():Paired { return paired("godot", true); }
		// Held by a static of AS3's alone.
		public function staticOnly():Int64 { Paired.held = paired("static", true); return Paired.held.getInstanceID(); }
		public function staticWord():String { return Paired.held ? Paired.held.label + " " + Paired.held.partnerWord() : "gone"; }
		public function letGoOfStatic():void { Paired.held = null; }
		// The plain half holds the Resource too: a loop through Godot's count as well as AS3's.
		public function throughGodot():Int64 {
			var made:Paired = paired("through", true);
			made.partner.res = made;
			return made.getInstanceID();
		}
		// A function written in a method listens to its object's own signal: held by nobody once this returns.
		public function closureLoop():Int64 {
			var made:Talker = new Talker();
			made.listen();
			return made.say() == 1 ? made.getInstanceID() : 0;
		}
		// The same, held by GDScript alone.
		public function closureHeld():Talker {
			var made:Talker = new Talker();
			made.listen();
			return made;
		}
		// Kept by Godot alone, in no loop: its fields must stay whole.
		public function keptByGodot():Paired { return paired("kept", false); }
		public function readWord():String { return Paired.read ? Paired.read.label + " " + Paired.read.partnerWord() : "gone"; }
		public function writtenWord():String { return Paired.written ? Paired.written.label : "gone"; }
		public function letGoOfRead():void { Paired.read = null; Paired.written = null; }
	}
}
