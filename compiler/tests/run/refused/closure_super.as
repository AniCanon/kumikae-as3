package { public class closure_super extends Base { public function closure_super() { var f:Function = function():void { super.go(); }; var g:Function = function():int { return super.n; }; var h:Function = function():int { return super.p; }; } } }
class Base { public var n:int; public function go():void {} public function get p():int { return 1; } }
