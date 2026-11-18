package {
	import godot.*;

	// Loops over Godot's own collections, and `in` and `delete` on its Dictionary: loops.gd does the same
	// in GDScript.
	public class Loops extends Node2D {
		public function childNames():String {
			var out:String = "";
			for each (var child:Node in getChildren()) {
				out += child.name + ";";
			}
			return out;
		}
		public function sum(list:GodotArray):Number {
			var total:Number = 0;
			for each (var n:Number in list) {
				total += n;
			}
			return total;
		}
		public function untypedElements(list:GodotArray):String {
			var out:String = "";
			for each (var value:* in list) {
				out += typeof value + ":" + value + ";";
			}
			return out;
		}
		public function indices(list:GodotArray):String {
			var out:String = "";
			for (var i:int in list) {
				out += i + "=" + list[i] + ";";
			}
			return out;
		}
		public function keys(table:GodotDictionary):String {
			var out:String = "";
			for (var key:* in table) {
				out += typeof key + ":" + key + ";";
			}
			return out;
		}
		public function textKeys(table:GodotDictionary):String {
			var out:String = "";
			var key:String;
			for (key in table) {
				out += key + "=" + table[key] + ";";
			}
			return out;
		}
		public function values(table:GodotDictionary):String {
			var out:String = "";
			for each (var value:* in table) {
				out += value + ";";
			}
			return out;
		}
		public function untilNegative(list:GodotArray):int {
			var count:int = 0;
			for each (var n:int in list) {
				if (n == 0) {
					continue;
				}
				if (n < 0) {
					break;
				}
				count++;
			}
			return count;
		}
		public function grown(list:GodotArray):int {
			var count:int = 0;
			for each (var n:int in list) {
				if (n == 1) {
					list.append(5);
				}
				count++;
			}
			return count;
		}
		public function erasedOnTheWay(table:GodotDictionary):String {
			var out:String = "";
			for (var key:String in table) {
				out += key + ";";
				table.erase("b");
				table["late"] = 1;
			}
			return out;
		}
		public function pairs(list:GodotArray):String {
			var out:String = "";
			for each (var a:int in list) {
				for each (var b:int in list) {
					if (a < b) {
						out += a + "" + b + ";";
					}
				}
			}
			return out;
		}
		public function packedIndices(list:PackedInt32Array):String {
			var out:String = "";
			for (var i:int in list) {
				out += i + "=" + list[i] + ";";
			}
			return out;
		}
		public function has(table:GodotDictionary, key:*):Boolean {
			return key in table;
		}
		public function remove(table:GodotDictionary, key:*):Boolean {
			return delete table[key];
		}
		public function removeNamed(table:GodotDictionary):int {
			delete table["b"];
			return int(table.size());
		}
		// Into a variable of the script's own class: a node that carries the script is one, as for `as`.
		public function scripted(list:GodotArray):String {
			var out:String = "";
			for each (var own:Loops in list) {
				out += own.name + ";";
			}
			return out;
		}
		public function points(list:GodotArray):Number {
			var total:Number = 0;
			for each (var point:Vector2 in list) {
				total += point.x * point.y;
			}
			return total;
		}
		// No collection: nothing to go through, as with a Vector that is null.
		public function missing():String {
			var list:GodotArray = null;
			var table:GodotDictionary = null;
			var count:int = 0;
			for each (var a:* in list) { count++; }
			for (var i:int in list) { count++; }
			for each (var b:* in table) { count++; }
			for (var k:* in table) { count++; }
			var said:String = "" + count;
			try { said += " " + ("k" in table); } catch (e:Error) { said += " in:" + e.errorID; }
			try { said += " " + (delete table["k"]); } catch (f:Error) { said += " delete:" + f.errorID; }
			return said;
		}
		public function wrongElement(list:GodotArray):String {
			var out:String = "";
			for each (var child:Node in list) {
				out += child.name;
			}
			return out;
		}
	}
}
