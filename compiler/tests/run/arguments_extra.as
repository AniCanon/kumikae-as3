package {
	// A function that names `arguments` takes more than it declares, as one with `...rest` does (avmplus's
	// MethodInfo: `_allowExtraArgs = _needRest || _needArguments || _ignoreRest`): what was passed beyond
	// its parameters is in `arguments` after them. Too few is still Error #1063, and so is too many for a
	// function that does not name it.
	public class arguments_extra {
		public function arguments_extra() {
			var sum:Function = function ():Number {
				var total:Number = 0;
				for (var i:int = 0; i < arguments.length; i++) {
					total += arguments[i];
				}
				return total;
			};
			trace(sum(), sum(1, 2, 3), sum.apply(null, [4, 5]), sum.call(null, 6), sum.length);
			var named:Function = function (first:int, second:String = "b"):String {
				return first + second + ":" + arguments.length + ":" + arguments.join("|") + ":" + (arguments.callee == named);
			};
			trace(named(1), named(1, "x"), named(1, "x", true, null, 2.5), named.length);
			var held:* = this;
			trace(held.counted(1, 2, 3), counted(7), held["counted"].apply(this, [1, 2, 3, 4]));
			var base:Plain = new Counting();
			var loose:* = base;
			trace(base.say(1), new Plain().say(2), loose.say(3, 4, 5));
			try {
				named();
			} catch (e:ArgumentError) {
				trace(e.errorID);
			}
			var strict:Function = function (a:int):int { return a; };
			try {
				strict(1, 2);
			} catch (e:ArgumentError) {
				trace(e.errorID);
			}
		}
		public function counted(p_first:int):String {
			return p_first + "/" + arguments.length;
		}
	}
}
class Plain {
	public function say(p_value:int):String {
		return "plain " + p_value;
	}
}
class Counting extends Plain {
	override public function say(p_value:int):String {
		return "counting " + p_value + " of " + arguments.length;
	}
}
