namespace mx_internal;
use namespace mx_internal;
include "other.as";
default xml namespace = ns;
class A {
	function f(o:Object):void {
		with (o) { g(); }
		var a:* = o::name;
		var b:XML = <node attr="1"/>;
		var c:* = o..child;
		var d:* = o.@attr;
	}
}
