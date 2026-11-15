package {
	public class number_text {
		public function number_text() {
			var i:int = -255;
			var u:uint = uint.MAX_VALUE;
			var n:Number = 1e21;
			var w:Int64 = Int64(123);
			trace((255).toString(16), i.toString(2), (0).toString(2), int.MAX_VALUE.toString(36), int.MIN_VALUE.toString(16), u.toString(16), u.toString());
			trace(n.toString(16), Math.pow(2, 60).toString(2), Math.pow(2, 60).toString(32), (1.5).toString(10), (1.5).toString(), (35).toString(36), (35).toString(2.9));
			trace(NaN.toString(2), Infinity.toString(16), (-Infinity).toString(2), Number.MAX_VALUE.toString(36).length);
			// AS3 writes only the whole part of a number in a radix other than 10 (ECMAScript today gives "ff.8").
			trace((255.5).toString(16), (-2.5).toString(2), (-0.5).toString(2));
			trace(w.toString(), w.toString(16), Int64.MAX_VALUE.toString(2), Int64.MIN_VALUE.toString(2));
			trace(Int64.MAX_VALUE.toString(16), Int64.MIN_VALUE.toString(16), Int64.MAX_VALUE.toString(36), Int64.MIN_VALUE.toString(36), Int64(-1).toString(16));
			trace((2.5).toFixed(0), (0.125).toFixed(2), (1.005).toFixed(2), (1.45).toFixed(1), (-1.5).toFixed(0), (-0.0001).toFixed(2), (0).toFixed(2));
			trace((1e21).toFixed(2), (123.456).toFixed(10), NaN.toFixed(2), Infinity.toFixed(1), (-Infinity).toFixed(0), (0.5).toFixed(), (-0.5).toFixed(0));
			trace((1.5e-7).toFixed(20), (7).toFixed(2), u.toFixed(1), (0.000001).toFixed(7), (999.995).toFixed(2), (1e20).toFixed(2), (0.006).toFixed(2));
			trace((123456).toExponential(2), (0).toExponential(2), (1.5).toExponential(0), (0.00015).toExponential(1), (-12.5).toExponential(1));
			trace(NaN.toExponential(2), (99.99).toExponential(1), (1).toExponential(20), (5e-324).toExponential(3), Infinity.toExponential(3), i.toExponential(3));
			// A digit count left out is the declared 0, as AVM2 passes it (ECMAScript's undefined writes every digit).
			trace((123.456).toExponential());
			trace((25).toPrecision(1), (123.456).toPrecision(4), (0.000123).toPrecision(2), (0.0000001234).toPrecision(2), (123456).toPrecision(2));
			trace((99.99).toPrecision(3), (0).toPrecision(3), (1e21).toPrecision(3), (-1.5).toPrecision(1), (123).toPrecision(3), (1.5).toPrecision(21));
			trace(NaN.toPrecision(0), (-Infinity).toPrecision(50), (0.000001).toPrecision(1), (1e-7).toPrecision(1), u.toPrecision(3));
			var big:Int64 = 9007199254740993;
			// An Int64 is written from its own digits: exact past 2^53, where a Number would round.
			trace(Int64.MAX_VALUE.toFixed(2), Int64.MIN_VALUE.toExponential(3), Int64.MAX_VALUE.toPrecision(21), Int64.MAX_VALUE.toPrecision(3), big.toExponential(15));
			trace(Int64(-5).toFixed(0), Int64(0).toExponential(1), Int64(15).toPrecision(1), Int64(-95).toPrecision(1), big.toFixed(1), Int64(0).toPrecision(2));
			try { trace(big.toFixed(21)); } catch (e:RangeError) { trace(e.errorID); }
			trace((5).valueOf(), (1.5).valueOf(), w.valueOf() == w, u.valueOf(), true.toString(), false.valueOf());
			try { trace((5).toString(1)); } catch (e:RangeError) { trace(e); }
			try { trace(i.toString(37)); } catch (e:RangeError) { trace(e); }
			try { trace(i.toString(-1)); } catch (e:RangeError) { trace(e); }
			try { trace(w.toString(0)); } catch (e:RangeError) { trace(e); }
			try { trace((1).toFixed(21)); } catch (e:RangeError) { trace(e.errorID); }
			try { trace(NaN.toFixed(-1)); } catch (e:RangeError) { trace(e.errorID); }
			try { trace((1).toExponential(21)); } catch (e:RangeError) { trace(e.errorID); }
			try { trace((1).toPrecision(22)); } catch (e:RangeError) { trace(e.errorID); }
			// toPrecision() is toPrecision(0) in AS3, out of its range.
			try { trace((1).toPrecision()); } catch (e:RangeError) { trace(e); }
		}
	}
}
