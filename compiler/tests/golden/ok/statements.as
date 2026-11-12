function run(list:Array, limit:int):int {
	var total:int = 0;
	outer: for (var i:int = 0; i < list.length; i++) {
		if (list[i] == null) continue outer;
		else if (total > limit) break;
		total += list[i];
	}
	for (var key:String in list) trace(key);
	for each (var item:* in list) { trace(item); }
	do { total--; } while (total > limit);
	while (false) ;
	switch (total) {
		case 0:
		case 1: return 1;
		default: throw new Error("bad");
	}
	try { risky(); } catch (e:TypeError) { trace(e); } catch (e) { } finally { done(); }
	return total;
}
