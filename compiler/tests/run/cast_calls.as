package {
    // `Type(value)` with a class or an interface of the program is AS3's cast: the value itself when it is of
    // the type, null for null and undefined, and TypeError #1034 for anything else. The conversions and the
    // library's own forms mean what they did.
    public class cast_calls {
        private var asked:int = 0;

        public function cast_calls() {
            var coin:CastCoin = new CastCoin("gold");
            var thing:Object = coin;
            var held:* = coin;
            var base:CastThing = coin;
            var named:CastNamed = coin;
            var missing:*;
            var none:CastThing = null;

            trace(CastCoin(thing) == coin, CastCoin(held) == coin, CastCoin(base) == coin, CastCoin(named) == coin, CastCoin(coin) == coin);
            trace(CastThing(coin) == coin, CastThing(held) == coin, CastNamed(thing) == coin, CastNamed(base) == coin, CastNamed(coin) == coin);
            trace(CastCoin(null), CastCoin(missing), CastCoin(none), CastNamed(null), CastNamed(missing), CastThing(none));
            trace(CastCoin(thing).name(), CastCoin(base).metal, CastNamed(held).name(), CastRound(named).round());
            trace(CastCoin(next(coin)).metal, asked);

            held = new CastThing();
            fails(function():void { CastCoin(held); });
            fails(function():void { CastNamed(held); });
            fails(function():void { CastRound(held); });
            held = "x";
            fails(function():void { CastCoin(held); });
            held = 5;
            fails(function():void { CastNamed(held); });
            held = true;
            fails(function():void { CastThing(held); });
            held = [1, 2];
            fails(function():void { CastCoin(held); });
            held = new <int>[1];
            fails(function():void { CastThing(held); });
            held = CastCoin;
            fails(function():void { CastCoin(held); });
            var plain:CastThing = new CastThing();
            fails(function():void { CastCoin(plain); });
            fails(function():void { CastOther(plain); });
            var shaped:CastNamed = new CastStone();
            fails(function():void { CastCoin(shaped); });
            fails(function():void { CastRound(shaped); });

            try {
                throw new CastError("mine");
            } catch (error:Error) {
                trace(CastError(error).message, CastError(error) == error, CastError(error).code);
            }
            var failure:Error = new Error("plain");
            fails(function():void { CastError(failure); });

            try {
                CastCoin(plain).name();
            } catch (error:TypeError) {
                trace("caught", error.errorID);
            } finally {
                trace("finally");
            }
            var stored:CastCoin = CastCoin(thing);
            var other:CastNamed = CastNamed(stored);
            trace(stored == coin, other == coin, typeof CastCoin(thing), CastCoin(thing) is CastNamed);

            var f:Function = next;
            var ints:Vector.<int> = Vector.<int>([1, 2, 3]);
            trace(String(5), Number("7"), int(3.9), uint(-1), Boolean("x"), Array(3).length, Object(coin) == coin);
            trace(Function(f) == f, Class(CastCoin) == CastCoin, ints.length, Vector.<int>(ints) == ints);
        }

        private function next(value:Object):Object {
            asked++;
            return value;
        }

        private function fails(cast:Function):void {
            try {
                cast();
                trace("no error");
            } catch (error:TypeError) {
                trace(error.errorID, error.message);
            }
        }
    }
}

interface CastNamed {
    function name():String;
}

interface CastRound extends CastNamed {
    function round():Boolean;
}

class CastThing {
}

class CastOther extends CastThing {
}

class CastCoin extends CastThing implements CastRound {
    public var metal:String;

    public function CastCoin(metal:String) {
        this.metal = metal;
    }

    public function name():String { return "coin"; }
    public function round():Boolean { return true; }
}

class CastStone implements CastNamed {
    public function name():String { return "stone"; }
}

class CastError extends Error {
    public var code:int = 7;

    public function CastError(message:String) {
        super(message);
    }
}
