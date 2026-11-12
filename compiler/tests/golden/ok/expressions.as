var a:int = 1 + 2 * 3 - 4 / 2 % 3;
var b:Boolean = !a || a > 1 && a <= 9 || a === 3;
var c:int = a << 2 >> 1 >>> 0 & 0xFF | 1 ^ 2;
var d:* = a ? b : c;
var e:Object = { x: 1, "y": [1, , 3], nested: { z: null } };
var f:int = (a += 2, a++, --a, -a);
var g:Boolean = e is Object && "x" in e && e instanceof Object;
var h:String = (e as Object).x.toString() + typeof e;
