package { public class closure_this { private var n:int; public function closure_this() { var f:Function = function():int { return this.n; }; } } }
