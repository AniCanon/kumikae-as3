package app.anicanon.kumikae.testing {
    public class TestProtocol {
        public static function emit(event:String, meta:Object, details:String = ""):void {
            var header:String = JSON.stringify(meta);
            trace("@kas3-test-v2", event, header.length, details.length);
            trace(header);
            trace(details);
        }

        public static function describeError(error:*):String {
            if (error is Error) {
                var stack:String = (error as Error).getStackTrace();
                if (stack != null && stack != "") {
                    return stack;
                }
                return (error as Error).name + ": " + (error as Error).message;
            }
            return String(error);
        }

        public static function matchesError(error:*, expected:String):Boolean {
            if (expected == null || expected == "") {
                return false;
            }
            if (error is Error && (error as Error).name == expected) {
                return true;
            }
            var text:String = String(error);
            return text == expected || text.indexOf(expected + ":") == 0;
        }

        public static function failureMeta(name:String, error:*):Object {
            var meta:Object = { name: name };
            if (error is AssertionError) {
                var assertion:AssertionError = error as AssertionError;
                if (assertion.hasComparison) {
                    meta.expected = assertion.expected;
                    meta.actual = assertion.actual;
                }
            }
            return meta;
        }
    }
}
