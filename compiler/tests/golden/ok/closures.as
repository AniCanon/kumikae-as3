function make(step:int):Function {
	var count:int = 0;
	function bump():int { return count += step; }
	var twice:Function = function (n:Number, ...rest):Number { return n * 2; };
	var anything:Function = function (a:*=null):* { return a; };
	list.sort(function (x:int, y:int):int { return x - y; });
	return bump;
}
