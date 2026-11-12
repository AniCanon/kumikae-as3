function g(event:Object, obj:Object):void {
	trace(event.type, obj.default, obj.class, obj.new, obj.in, obj.function);
	var literal:Object = { default: 1, class: 2, function: 3, if: 4 };
	var get:int = 1, set:int = 2, each:int = 3, static:int = 4, namespace:int = 5, include:int = 6;
	get = set + each;
}
