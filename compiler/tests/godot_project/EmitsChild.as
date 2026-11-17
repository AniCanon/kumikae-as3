package {
	// A script whose class extends another script's: on a node, it is an Emits too (emits_cases.gd).
	public class EmitsChild extends Emits {
		public function childName():String { return "child " + name; }
	}
}
