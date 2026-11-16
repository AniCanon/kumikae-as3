package { public class interface_inherited { public function interface_inherited() { var t:Text = new Plain(); trace(t.toString()); } } }
interface Text { function toString():String; }
class Plain implements Text {}
