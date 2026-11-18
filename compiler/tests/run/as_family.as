package {
    // `x as T` gives the value when it is a T, and null when it is not: for the number types and Boolean too,
    // whatever the value's own type, and wherever the result is used.
    public class as_family {
        private var number:Number = 1;
        private var any:* = 1;

        public function as_family() {
            var u:* = "x";
            var o:Object = 3.5;
            var i:int = 5;
            var n:Number = 3.5;
            var whole:Number = 4;
            var neg:Number = -1;
            var big:Number = 4294967295;
            var b:Boolean = true;
            var s:String = "5";
            var missing:*;

            trace("x" as Number);
            trace(("x" as Number) == null, ("x" as Number) === null, ("x" as Number) != null);
            trace(u as Number, u as int, u as uint, u as Boolean, u as String);
            trace(o as Number, o as int, o as uint, o as Boolean, o as String);
            trace(i as Number, i as int, i as uint, i as Boolean, i as String);
            trace(n as Number, n as int, n as uint, n as Boolean, n as String);
            trace(whole as Number, whole as int, whole as uint);
            trace(neg as Number, neg as int, neg as uint);
            trace(big as Number, big as int, big as uint);
            trace(2147483647 as int, 2147483648 as int, -2147483648 as int, -2147483649 as int, 1e10 as uint);
            trace(NaN as Number, NaN as int, NaN as uint, Infinity as Number, Infinity as int);
            trace(b as Boolean, b as Number, b as int, b as String);
            trace(s as String, s as Number, s as int, s as Boolean);
            trace(0 as Boolean, 1 as Boolean, false as Boolean, "" as Boolean, "" as String);
            trace(null as Number, null as int, null as uint, null as Boolean, null as String, null as Object);
            trace(missing as Number, missing as int, missing as uint, missing as Boolean, missing as String, missing as Object);
            trace(5 as Object, "t" as Object, true as Object, o as Object);

            var a:Number = "x" as Number;
            var c:int = 3.5 as int;
            var d:Boolean = 0 as Boolean;
            var e:uint = neg as uint;
            var f:* = "x" as Number;
            var g:Object = 3.5 as int;
            var h:String = i as String;
            trace(a, c, d, e, f, g, h);
            number = u as Number;
            any = u as Number;
            trace(number, any);
            number = n as Number;
            any = i as int;
            trace(number, any);

            trace(("x" as Number) + 1, (n as Number) + 1, (u as int) * 2, (i as int) - 1, ("x" as Number) < 1);
            trace("v=" + ("x" as Number), "v=" + (i as Number), ("x" as Boolean) + "!");
            trace((1 as Number) ? "yes" : "no", ("x" as Number) ? "yes" : "no", (false as Boolean) ? "yes" : "no", !(u as Boolean));
            trace(("x" as Number) || 7, (2 as Number) && 3, ("x" as int) && 3);
            trace(b ? ("x" as int) : 5, b ? (i as int) : 6);
            trace(typeof ("x" as Number), typeof (1 as Number), typeof (u as Boolean), typeof (b as Boolean), typeof (s as String));
            trace(("x" as Number) is Number, (1 as Number) is Number, (i as int) is int, (n as int) is int);
            trace(takesNumber("x" as Number), takesNumber(n as Number), takesInt(n as int), takesBoolean(1 as Boolean));
            trace(takesAny("x" as Number), takesAny(3 as int), takesObject(n as int), takesObject(n as Number));
            trace(givesNumber(u), givesAny(u), givesObject(i), givesAny(n));
            var t:Number = 1;
            t += ("x" as Number);
            t += (n as Number);
            trace(t);
            var list:Array = [10, 20];
            trace(list[0 as int], list[1 as uint], list["x" as int]);
            trace((n as Number).toFixed(1), (s as String).length, (i as int).toString());
            try {
                trace(("x" as Number).toFixed(1));
            } catch (error:TypeError) {
                trace(error.errorID);
            }
            try {
                trace((5 as String).length);
            } catch (error:TypeError) {
                trace(error.errorID);
            }

            var coin:AsCoin = new AsCoin();
            var thing:Object = coin;
            var held:* = coin;
            var named:AsNamed = coin;
            trace((thing as AsCoin) == coin, (held as AsCoin) == coin, (thing as AsOther) == null, (coin as AsNamed) == coin, (thing as AsNamed) == coin);
            trace((named as AsCoin) == coin, (named as AsOther) == null, (u as AsCoin) == null, (5 as AsCoin) == null, (missing as AsCoin) == null);
            trace(coin as Number, coin as int, coin as uint, coin as Boolean, coin as String, (coin as Object) == coin);
            trace(named as Number, named as String, (null as AsCoin) == null, (null as AsNamed) == null);
            trace(thing as Number, held as Boolean, (thing as AsCoin).name(), (held as AsNamed).name());
            var array:Array = [1];
            var arrayHeld:Object = array;
            var ints:Vector.<int> = new <int>[1];
            var intsHeld:* = ints;
            trace((arrayHeld as Array) == array, (u as Array) == null, (intsHeld as Vector.<int>) == ints, (intsHeld as Vector.<Number>) == null, (arrayHeld as Vector.<int>) == null);
            trace(array as Number, ints as int, (array as Object) == array);
        }

        private function takesNumber(value:Number):Number { return value; }
        private function takesInt(value:int):int { return value; }
        private function takesBoolean(value:Boolean):Boolean { return value; }
        private function takesAny(value:*):* { return value; }
        private function takesObject(value:Object):Object { return value; }
        private function givesNumber(value:*):Number { return value as Number; }
        private function givesAny(value:*):* { return value as Number; }
        private function givesObject(value:int):Object { return value as int; }
    }
}

interface AsNamed {
    function name():String;
}

class AsCoin implements AsNamed {
    public function name():String { return "coin"; }
}

class AsOther {
}
