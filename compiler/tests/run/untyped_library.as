package { public class untyped_library {
	public function untyped_library() {
		// A RegExp and a Date held untyped: their properties and methods by name, as typed ones have them.
		var r:* = /o(b)?/g;
		trace(r.source, r.global, r.ignoreCase, r.multiline, r.dotall, r.extended, r.lastIndex);
		r.lastIndex = 2;
		trace(r.exec("foo"), r.lastIndex, r["lastIndex"]);
		trace(r.test("foo"), r.lastIndex, r.test(), r.lastIndex);
		var m:Array = r.exec("xob");
		trace(m.index, m[1], r.lastIndex);
		trace("lastIndex" in r, "exec" in r, "source" in r, "flags" in r);
		var d:* = new Date(2024, 2, 5, 14, 7, 9, 250);
		trace(d.fullYear, d.month, d.date, d.day, d.hours, d.minutes, d.seconds, d.milliseconds);
		trace(d.getFullYear(), d.getMonth(), d.getHours(), d.getTime() == d.time, d.valueOf() == d.time);
		d.fullYear = 2000;
		d.month = 0;
		trace(d.fullYear, d.month, d.getDate());
		trace(d.setHours(1, 2, 3) == d.time, d.hours, d.minutes, d.seconds);
		d.time = 0;
		trace(d.time, d.getUTCFullYear(), d.toUTCString(), d.fullYearUTC, d.dayUTC);
		d.setUTCHours(13);
		trace(d.hoursUTC, d.getUTCHours());
		trace("time" in d, "getTime" in d, "timezoneOffset" in d, "nothing" in d);
		// What a typed one refuses, said as Flash says it.
		try { r.source = "x"; } catch (e:Error) { trace(e.errorID, e.message); }
		try { r.exec = null; } catch (e:Error) { trace(e.errorID, e.message); }
		try { d.day = 3; } catch (e:Error) { trace(e.errorID, e.message); }
		try { d.timezoneOffset = 3; } catch (e:Error) { trace(e.errorID, e.message); }
		try { trace(r.nothing); } catch (e:Error) { trace(e.errorID, e.message); }
		try { d.nothing = 1; } catch (e:Error) { trace(e.errorID, e.message); }
		// A method read and not called can't be made yet.
		try { var f:* = r.exec; trace(f); } catch (e:Error) { trace(e.message); }
		try { var g:* = d.getTime; trace(g); } catch (e:Error) { trace(e.message); }
	}
} }
