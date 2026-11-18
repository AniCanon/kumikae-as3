package {
    // A call of a class of the library that is no cast and no conversion: in Flash, Date(x) gives the time
    // now as text, RegExp(x) the expression itself or a new one, Error(x) a new error.
    public class class_call {
        public function class_call() {
            var held:* = 5;
            trace(Date(held));
            trace(RegExp(held));
            trace(Error(held));
        }
    }
}
