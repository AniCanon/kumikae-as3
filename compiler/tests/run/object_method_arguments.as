package {
	// One of Object's methods called on a value typed Object is the method of what the value is: a Number's
	// toString takes a radix.
	public class object_method_arguments {
		public function object_method_arguments() {
			var number:Object = 255;
			var fraction:Object = 2.5;
			var text:Object = "text";
			var list:Object = [1, 2];
			var plain:Object = { a: 1 };
			trace(number.toString(16), number.toString(2), number.toString(), fraction.toString(2), number.valueOf());
			trace(text.toString(), list.toString(), plain.toString(), plain.hasOwnProperty("a"), text.valueOf());
			var radix:int = 8;
			var words:String = number.toString(radix);
			trace(words, words.length);
		}
	}
}
