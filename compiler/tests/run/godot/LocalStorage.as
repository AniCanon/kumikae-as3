package {
	import godot.*;

	public class LocalStorage extends Node {
		private var amount:Number = 3.5;
		private function truth():Boolean { return true; }

		private function twice(value:Number):Number { return value * 2; }
		public function ordinaryInferred(source:Number, enabled:Boolean):Number {
			var amount:* = source;
			var flag:* = enabled;
			var box:* = new ScoreBox();
			var total:Number = 0;
			if (flag && amount > 0) total += amount;
			if (flag) total += twice(amount);
			total += box.score + 1;
			return total + amount * 2;
		}
		public function ordinaryDeclared(source:Number, enabled:Boolean):Number {
			var amount:Number = source;
			var flag:Boolean = enabled;
			var box:ScoreBox = new ScoreBox();
			var total:Number = 0;
			if (flag && amount > 0) total += amount;
			if (flag) total += twice(amount);
			total += box.score + 1;
			return total + amount * 2;
		}
		public function wrappedNumberInferred(flag:Boolean):Number {
			if (flag) { var local:* = this.amount; return local; } return 0;
		}
		public function wrappedNumberDeclared(flag:Boolean):Number {
			if (flag) { var local:Number = this.amount; return local; } return 0;
		}
		public function callbackInferred(read:()=>Number):Number {
			var local:* = read(); return local;
		}
		public function callbackDeclared(read:()=>Number):Number {
			var local:Number = read(); return local;
		}
		public function engineResultInferred(source:Vector2):Number {
			var local:* = source.length(); return local;
		}
		public function engineResultDeclared(source:Vector2):Number {
			var local:Number = source.length(); return local;
		}
		public function answerInferred():Boolean {
			var local:* = truth(); return local;
		}
		public function answerDeclared():Boolean {
			var local:Boolean = truth(); return local;
		}
		public function textLengthInferred():int {
			var local:* = "abc"; return local.length;
		}
		public function textLengthDeclared():int {
			var local:String = "abc"; return local.length;
		}
		public function boxScoreInferred():int {
			var local:* = new ScoreBox(); return local.score;
		}
		public function boxScoreDeclared():int {
			var local:ScoreBox = new ScoreBox(); return local.score;
		}
		public function argumentInferred(source:ScoreBox):ScoreBox {
			var local:* = source; return local;
		}
		public function argumentDeclared(source:ScoreBox):ScoreBox {
			var local:ScoreBox = source; return local;
		}
		public function vectorInferred():Number {
			var local:* = new Vector2(3, 4); return local.length();
		}
		public function vectorDeclared():Number {
			var local:Vector2 = new Vector2(3, 4); return local.length();
		}
		public function colorInferred():Number {
			var local:* = new Color(1, 0.5, 0); return local.g;
		}
		public function colorDeclared():Number {
			var local:Color = new Color(1, 0.5, 0); return local.g;
		}
		public function freshInferred():int {
			var local:* = new Gradient(); return local.getPointCount();
		}
		public function freshDeclared():int {
			var local:Gradient = new Gradient(); return local.getPointCount();
		}
		public function straightInferred(source:String):String {
			var local:*; { local = source; } return local;
		}
		public function straightDeclared(source:String):String {
			var local:String; { local = source; } return local;
		}
		public function laterInferred(source:String, flag:Boolean):String {
			var local:*; if (flag) { local = source; return local; } return "absent";
		}
		public function laterDeclared(source:String, flag:Boolean):String {
			var local:String; if (flag) { local = source; return local; } return "absent";
		}
	}
}

final class ScoreBox {
	public var score:int = 7;
}
