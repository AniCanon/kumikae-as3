package {
	// A SyntaxError from JSON.parse that nothing catches: reported at the line of the call.
	public class err_json_parse {
		public function err_json_parse() {
			trace(JSON.stringify(JSON.parse("[1]")));
			JSON.parse("[1,]");
		}
	}
}
