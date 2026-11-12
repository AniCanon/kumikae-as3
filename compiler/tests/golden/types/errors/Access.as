package {
	import godot.Node2D;

	public class Access extends Node2D {
		private var stash:int;
		public static var shared:int;

		public function run(other:Access, vault:Vault, text:String):void {
			other.stash = 1;
			other.shared = 2;
			Access.stash = 3;
			Access.missing = 4;
			vault.secret = 5;
			vault.guarded = 6;
			vault.open().secret;
			text.size;
			position.z;
			rotation.toFixed(1).nope;
			addChild(other).anything;
			unknown_name.anything.at.all;
		}
	}
}

class Vault {
	private var secret:int;
	protected var guarded:int;

	public function open():Vault {
		return this;
	}
}
