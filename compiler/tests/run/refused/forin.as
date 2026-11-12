package { public class forin { public function forin() { var o:Object = new Object(); for (var k:String in o) { trace(k); } for each (var v:int in o) { trace(v); } } } }
