package { public class untyped_number_methods {
	// A number's methods called by name on an untyped value: what the typed call gives.
	public function untyped_number_methods() {
		var i:* = 255;
		var n:* = 1234.5678;
		var wide:* = Int64(9007199254740993);
		var typedInt:int = 255;
		var typedNumber:Number = 1234.5678;
		trace(i.toString(16), i.toString(2), i.toString(), n.toString(10), n.toString(2) == typedNumber.toString(2));
		trace(i.toFixed(2), n.toFixed(1), n.toFixed(), n.toFixed(1) == typedNumber.toFixed(1));
		trace(i.toExponential(1), n.toExponential(3), n.toExponential() == typedNumber.toExponential());
		trace(i.toPrecision(2), n.toPrecision(6), i.toPrecision(5) == typedInt.toPrecision(5));
		trace(wide.toString(16), wide.toString(), wide.toFixed(1));
		var nan:* = NaN;
		trace(nan.toFixed(1), nan.toExponential(2), nan.toPrecision(3), nan.toString(2));
		// The digits are converted as the declared parameter is.
		trace(n.toFixed("2"), i.toString("16"));
		// Out of range: AS3's RangeErrors, as the typed calls raise.
		try { i.toString(1); } catch (e:RangeError) { trace("caught", e.message); }
		try { n.toFixed(21); } catch (e:RangeError) { trace("caught", e.message); }
		try { n.toPrecision(); } catch (e:RangeError) { trace("caught", e.message); }
		try { typedNumber.toFixed(21); } catch (e:RangeError) { trace("caught", e.message); }
		// On what is not a number the name is no method of it.
		var text:* = "abc";
		try { text.toFixed(1); } catch (e:Error) { trace("caught", e.message.indexOf("toFixed") >= 0); }
		// A String's locale case methods by name are its plain ones.
		trace(text.toLocaleUpperCase(), text.toLocaleLowerCase());
	}
} }
