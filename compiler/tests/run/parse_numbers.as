package {
	public class parse_numbers {
		public function parse_numbers() {
			trace(parseInt("12"), parseInt("  -0x1F"), parseInt("0x"), parseInt("12px"), parseInt("px"), parseInt(""), parseInt());
			trace(parseInt("101", 2), parseInt("z", 36), parseInt("Z", 36), parseInt("12", 1), parseInt("12", 37), parseInt("0x10", 16), parseInt("0x10", 10));
			trace(parseInt("123456789012345678901234567890"), parseInt("9007199254740993"), parseInt("1fffffffffffff1", 16), parseInt("\u00A0 7"));
			// A leading 0 is not octal in AS3, as its reference says.
			trace(1 / parseInt("-0"), parseInt("  +5  "), parseInt("08"), parseInt("1e3"), parseInt(" 12 34"), parseInt("Infinity"), parseInt("123", 0), parseInt("11", 4294967298));
			trace(parseInt("-ff", 16), parseInt("777", 8), parseInt("789", 8), parseInt("0x", 16), parseInt("-"), parseInt(null), parseInt("1" + "0000000000".substr(0, 9) + "0".substr(0, 0), 10));
			trace(parseFloat("3.14abc"), parseFloat("  -.5e-3x"), parseFloat(".e1"), parseFloat("1e"), parseFloat("Infinityx"), parseFloat("-Infinity"), parseFloat("0x10"));
			trace(parseFloat(""), parseFloat("1.5e400"), parseFloat("+1"), parseFloat("e5"), parseFloat("\u2003 2.5"), parseFloat("1.2.3"), 1 / parseFloat("-0"), parseFloat());
			trace(parseFloat("5."), parseFloat("-.e"), parseFloat("1e+"), parseFloat("1E2"), parseFloat("+-1"), parseFloat(null), parseFloat("0.1e-1xyz"));
			var none:Number;
			trace(isNaN(NaN), isNaN(5), isNaN(), isNaN(none), isFinite(Infinity), isFinite(-1.5), isFinite(NaN), isFinite(), isFinite(-Infinity), isFinite(0));
			trace(Number(" \u00A0 12 "), Number("\u3000"), int("\u00A0 7"), Number("0x1A"), int("  -12.9 "), uint("-1"), Boolean("false"), Boolean(NaN), String(1.5), String(true));
		}
	}
}
