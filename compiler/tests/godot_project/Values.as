package {
	import godot.*;

	// Godot's values in AS3: each method is called by values.gd, which works the same thing out in GDScript.
	public class Values extends Node2D {
		public var calls:int = 0;

		public function add(a:Vector2, b:Vector2):Vector2 { return a + b; }
		public function sub(a:Vector2, b:Vector2):Vector2 { return a - b; }
		public function times(a:Vector2, n:Number):Vector2 { return a * n; }
		public function scaleInt(a:Vector2, n:int):Vector2 { return a * n; }
		public function scaleLeft(n:Number, a:Vector2):Vector2 { return n * a; }
		public function divide(a:Vector2, n:Number):Vector2 { return a / n; }
		public function negate(a:Vector2):Vector2 { return -a; }
		public function cells(a:Vector2i, n:int):Vector2i { return a * n; }
		public function cellsNumber(a:Vector2i, n:Number):Vector2 { return a * n; }
		public function cellsOver(a:Vector2i, n:int):Vector2i { return a / n; }
		public function tint(a:Color, b:Color):Color { return a * b; }
		public function same(a:Vector2, b:Vector2):Boolean { return a == b; }
		public function differs(a:Vector2, b:Vector2):Boolean { return a != b; }
		public function sameNode(a:Node, b:Node):Boolean { return a == b; }
		public function otherNode(a:Node, b:Node):Boolean { return a != b; }
		public function chain(a:Vector2, b:Vector2):Vector2 { return (a + b) * 2 - a / 2; }
		public function compound(a:Vector2, b:Vector2):Vector2 {
			var v:Vector2 = a;
			v += b;
			v *= 3;
			return v;
		}
		public function moved(by:Vector2):Vector2 { return position + by; }

		// Text.
		private var kept:Node;
		public function text(v:Vector2):String { return "at " + v; }
		public function textAfter(v:Vector2):String { return v + " here"; }
		public function cast(v:Vector2):String { return String(v); }
		public function own(v:Vector2):String { return v.toString(); }
		public function colorText(c:Color):String { return String(c); }
		public function nodeText(n:Node):String { return n.toString(); }
		public function nodeJoined(n:Node):String { return "node " + n; }
		public function nullText():String { var v:Vector2 = null; return "v=" + v; }
		public function nullNode(n:Node):String { return String(n); }
		public function nullOwn(n:Node):String { return n.toString(); }
		public function traced(v:Vector2):void { trace(v, v.x); }
		public function keep(n:Node):void { kept = n; keptReady = n.ready; }
		// What is reached through a freed object, in every way: AS3's #1009, saying it was freed.
		private var keptReady:Signal;
		private function heard():void {}
		public function keptSignal():void { keptReady.add(heard); }
		public function keptByName():void { kept["ready"].add(heard); }
		public function keptText():String { return "kept " + kept; }
		// A Godot object that has been freed is null to a script.
		public function keptIsNull():Boolean { return kept == null; }
		public function keptIsSet():Boolean { return kept != null; }
		public function keptCounts():Boolean { return kept ? true : false; }
		public function keptNot():Boolean { return !kept; }
		public function keptName():String { return kept.name; }
		public function keptRename():void { kept.name = "renamed"; }
		public function keptChildren():int { return kept.getChildCount(); }
		public function keptUnder(parent:Node):Boolean { return parent.isAncestorOf(kept); }

		// One part of a value: read, change, write back.
		private var held:Vector2 = new Vector2(1, 2);
		public function setX(n:Number):Vector2 { position.x = n; return position; }
		public function addX(n:Number):Vector2 { position.x += n; return position; }
		public function bumpX():Vector2 { position.x++; return position; }
		public function nested(n:Number):Vector2 { transform.origin.y = n; return position; }
		public function heldPart(n:Number):Vector2 { held.y = n; return held; }
		public function localPart(v:Vector2, n:Number):Vector2 {
			var w:Vector2 = v;
			w.x = n;
			return w + v; // v is not changed: w is a copy.
		}
		public function next():Values { calls++; return this; }
		public function otherPart(n:Number):int { next().position.y = n; return calls; }
		public function fromItself():Vector2 { position.x = position.y; return position; }

		// By index.
		public function first(v:Vector2):Number { return v[0]; }
		public function colorPart(c:Color):Number { return c[1]; }
		public function setSecond(v:Vector2, n:Number):Vector2 { var w:Vector2 = v; w[1] = n; return w; }
		public function addElement(v:Vector2, n:Number):Vector2 { var w:Vector2 = v; w[0] += n; return w; }
		public function positionElement(n:Number):Vector2 { position[0] = n; return position; }
		public function packed(p:PackedInt32Array, i:int):int { return p[i]; }
		public function setPacked(p:PackedInt32Array, i:int, n:int):PackedInt32Array { p[i] = n; return p; }
		public function outOfRange(v:Vector2):Number { return v[5]; }

		// Arguments left out: Godot fills in its own defaults.
		public function clampDefault(c:Color):Color { return c.clamp(); }
		public function clampOne(c:Color):Color { return c.clamp(new Color(0.25, 0.25, 0.25, 1)); }
		public function clampTrailingNull(c:Color):Color { return c.clamp(new Color(0.25, 0.25, 0.25, 1), null); }
		public function clampBothNull(c:Color):Color { return c.clamp(null, null); }
		public function clampMiddleNull(c:Color):Color { return c.clamp(null, new Color(0.5, 0.5, 0.5, 1)); }
		// A variable that holds null there is the same: the default, looked at as the call is made.
		public function clampNullVariable(c:Color):Color { var low:Color = null; return c.clamp(low, new Color(0.5, 0.5, 0.5, 1)); }
		public function looking(t:Transform3D, target:Vector3):Transform3D { return t.lookingAt(target); }
		public function lookingNull(t:Transform3D, target:Vector3):Transform3D { return t.lookingAt(target, null); }
		public function wholeNumbers():Color { return new Color(1, 0, 1); }
		public function describe():String { return "I am " + this; }

		// What the review of this slice found.
		public function negCells(c:Vector2i):Vector2i { return c * -1; }
		public function cellsPlus(c:Vector2i, n:int):Vector2i { return c * (n + 1); }
		public function cellsCompound(c:Vector2i, n:int):Vector2i { var d:Vector2i = c; d *= n + 1; return d; }
		public function bump():Number { position = new Vector2(100, 200); return 1; }
		public function plainOrder():Vector2 { position = new Vector2(1, 2); position.x = bump(); return position; }
		public function compoundOrder():Vector2 { position = new Vector2(1, 2); position.x += bump(); return position; }
		public function mixed(v:Vector2, n:Node):Boolean { return v == 5 || n == "x" || !(v != 5); }
		public function bigHash(a:GodotArray):Number { return a.hash(); }
		public function decode(b:PackedByteArray):Number { return b.decodeU32(0); }
	}
}
