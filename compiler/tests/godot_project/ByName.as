package {
	import godot.*;

	// Godot's values and objects held untyped, used by name. by_name.gd works the same thing out in GDScript,
	// whose untyped variables do the same.
	public class ByName extends Node2D {
		public var score:int = 5;
		public var heard:int = 0;
		public function twice(n:int):int { return n * 2; }
		private function onChanged():void { heard++; }

		public function reads(o:*):Array { return [o.name, o["name"], o.visible, o.position, o.getChildCount(), o.getParent() == this, o.name is String]; }
		public function writes(o:*):Array {
			o.position = new Vector2(3, 4);
			o.visible = false;
			o["name"] = "renamed";
			var at:Vector2 = o.position;
			at.x += 1;
			o.position = at;
			return [o.position, o.visible, o.name];
		}
		public function calls(o:Object):Array {
			o.show();
			var child:* = new Node();
			o.addChild(child);
			child.name = "inner";
			return [o.visible, o.getChildCount(), o.getChild(0).name, o.isAncestorOf(child), o.getNode("inner") == child];
		}
		// A name that is not there, in AS3's numbers and words.
		public function missing(o:*):Array {
			var said:Array = [];
			try { o.nothing; } catch (e:ReferenceError) { said.push(e.message); }
			try { o.nothing = 1; } catch (e:ReferenceError) { said.push(e.message); }
			try { o.nothing(); } catch (e:TypeError) { said.push(e.message); }
			try { o.hide = 1; } catch (e:ReferenceError) { said.push(e.errorID); }
			try { o.name(); } catch (e:TypeError) { said.push(e.errorID); }
			try { o.hide(1); } catch (e:Error) { said.push("refused"); }
			try { o.position = "text"; } catch (e:Error) { said.push("refused"); }
			return said;
		}
		public function has(o:*):Array { return ["name" in o, "nothing" in o, o.hasOwnProperty("hide"), o.hasOwnProperty("nothing")]; }
		// One of Godot's values: its parts and its methods.
		public function ofValue():Array {
			var v:* = new Vector2(3, 4);
			var color:Object = new Color(1, 0.5, 0);
			var said:String = "written";
			try { v.x = 6; } catch (e:Error) { said = "a copy"; }
			return [v.x, v.y, v.length(), v.normalized().y, color.g, color.lightened(0.5).r, v is Vector2, said];
		}
		// A node that carries a script of the program: the script's members first, then Godot's.
		public function scripted(o:*):Array {
			o.score = 9;
			o.hide();
			return [o.score, o.twice(4), o.name, o.visible, "score" in o, "twice" in o, "hide" in o];
		}
		// The script's own object held untyped reaches its Godot object's members too.
		public function self():Array {
			var me:* = this;
			me.rotation = 0.5;
			me.show();
			return [me.score, me.twice(3), me.name, me.rotation, me.visible, me.getChildCount()];
		}
		// A method read and not called is a function, the one a typed read gives.
		public function method(o:*):Array {
			var hide:Function = o.hide;
			var typed:Node2D = o;
			o.show();
			hide();
			return [o.visible, hide == o.hide, hide == typed.hide, typeof hide];
		}
		// A signal reached by name takes a listener.
		public function listens(o:*):Array {
			heard = 0;
			o.visibilityChanged.add(onChanged);
			var has:Boolean = o.visibilityChanged.has(onChanged);
			o.visible = !o.visible;
			o.visibilityChanged.remove(onChanged);
			o.visible = !o.visible;
			return [has, heard, o.visibilityChanged.numListeners];
		}
		public function exactArguments(o:*):Array {
			var b:* = false;
			var v:* = new Vector2(6, 8);
			var n:* = 0.5;
			o.setProcess(b);
			o.translate(v);
			o.rotate(n);
			return [o.isProcessing(), o.position, o.rotation, v.dot(v)];
		}
		public function missingKeys(target:*, count:int):int {
			var found:int = 0;
			for (var i:int = 0; i < count; i++) {
				if (("slot" + i) in target) found++;
			}
			return found;
		}
		public function attachedTimer(script:Script):Number {
			var timer:* = new Timer();
			var before:Number = timer.waitTime;
			timer.setScript(script);
			var after:Number = timer.waitTime;
			timer.free();
			return before + after;
		}
		public function actionValues(o:*, key:String, sink:*):Array {
			var literal:* = o.action;
			var computed:* = o[key];
			return [literal is Function, computed is Function, sink.invoke(literal), sink.invoke(computed)];
		}
		public function rotatingKeys(o:*):int {
			var keys:Vector.<String> = new Vector.<String>();
			var i:int;
			var sum:int = 0;
			for (i = 0; i < 16; ++i) keys.push("allocation_probe_rotating_" + i);
			for (i = 0; i < 32; ++i) sum += int(o[keys[i % 16]]);
			for (i = 0; i < 80; ++i) {
				sum += int(o[keys[0]]);
				sum += int(o["cold_field_" + i]);
			}
			return sum;
		}
		public function readMany(o:*):int {
			var sum:int = 0;
			for (var i:int = 0; i < 16; i++) sum += int(o.allocation_probe);
			return sum;
		}
		public function readStored(o:*):* {
			try { return o.stored; }
			catch (e:ReferenceError) { return e.errorID; }
		}
		public function writeKey(o:*, key:String):int {
			try { o[key] = 17; return int(o[key]); }
			catch (e:Error) { return -e.errorID; }
		}
		public function removedDuringCall(o:*, key:String, editor:*):Array {
			var before:int = writeKey(o, key);
			editor.replace_schema(o);
			try { return [before, o[key]]; }
			catch (e:ReferenceError) { return [before, e.errorID]; }
			catch (other:Error) { return [before, other.message]; }
		}
		public function writeAcrossReload(o:*, key:String, script:Script, editor:*, source:String):Array {
			var before:int = writeKey(o, key);
			if (source == null) {
				script.reload(true);
			} else {
				script.sourceCode = source;
				editor.replace_schema(o);
			}
			return [before, writeKey(o, "added")];
		}
		public function readAcrossReload(o:*, key:String, editor:*):Array {
			var before:* = readStored(o);
			var had:Boolean = key in o;
			editor.replace_schema(o);
			return [before, had, readStored(o), key in o];
		}
		public function readAcrossTypedReload(o:*, key:String, script:Script):Array {
			var before:* = readStored(o);
			var had:Boolean = key in o;
			script.reload(true);
			return [before, had, readStored(o), key in o];
		}
		public function foreignDeclarations(o:*, property:String, method:String):Array {
			return [o.stored, o.script_score(), o[property], o[method]()];
		}
		public function foreignVisible(o:*):Boolean { o.visible = false; return o.visible; }
		public function aliasVisible(source:Node2D):Boolean {
			var alias:* = source;
			alias.visible = false;
			var seen:Boolean = alias.visible;
			return seen;
		}

		private function sumPositions(source:Node2D, count:int):Number {
			var target:* = source;
			var sum:Number = 0;
			for (var i:int = 0; i < count; i++) {
				var point:Vector2 = target.position;
				sum += point.x;
			}
			return sum;
		}
		public function releasedBeforeReturn(target:*, maker:*):Boolean {
			var held:* = maker.make(target);
			held = null;
			return target.getScript() != null;
		}
		public function releaseWithAlias(target:*, maker:*):Boolean {
			var owner:* = maker.make(target);
			var copy:* = owner;
			copy = null;
			return target.getScript() != null;
		}
		public function computedPositions(target:*, key:String):Array {
			var keys:Vector.<String> = new Vector.<String>();
			keys.push(key);
			var values:Array = [];
			var value:*;
			for (var i:int = 0; i < 5; ++i) {
				value = target[keys[0]];
				values.push(value);
			}
			return values;
		}
		public function aliasPositions(source:Node2D):Array {
			var target:* = source;
			target.position = new Vector2(8, 9);
			var total:Number = sumPositions(source, 2);
			var failed:Boolean = false;
			try { sumPositions(source, 3); } catch (e:Error) { failed = true; }
			return [total, failed];
		}

		public function freeAfterRead(o:*):String {
			var first:String = "";
			for (var i:int = 0; i < 2; i++) {
				try { first = String(o.name); }
				catch (e:TypeError) { return first + "/" + e.errorID; }
				var node:Node = o;
				node.free();
			}
			return first;
		}

		private function component(source:Vector2):Number { var point:* = source; return point.x; }
		private function channel(source:Color):Number { var color:* = source; return color.g; }
		public function nullParts():Array {
			var errors:Array = [];
			try { errors.push(component(null)); } catch (pointError:Error) { errors.push(pointError.errorID); }
			try { errors.push(channel(null)); } catch (colorError:Error) { errors.push(colorError.errorID); }
			return errors;
		}
		// A freed object is null.
		public function freed(o:*):String {
			try { o.name; } catch (e:TypeError) { return "TypeError " + e.errorID; }
			return "read";
		}
		private function observed(value:*):* {
			if (value is Function) return value();
			return value;
		}
		public function engineReads(target:*, method:String, property:String, plain:String):Array {
			return [observed(target.isProcessing), observed(target[method]),
				target.position, target[property], observed(target.isVisibleInTree), observed(target[plain])];
		}
		private var savedMethods:Array = [];
		public function saveMethodReads(target:*, key:String):void {
			for (var i:int = 0; i < 5; ++i) savedMethods.push(target[key]);
		}
		public function callReadMethods():Array {
			var results:Array = [];
			for each (var method:* in savedMethods) results.push(method());
			savedMethods = [];
			return results;
		}
	}
}
