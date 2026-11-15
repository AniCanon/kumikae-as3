package {
	// Date. run_programs.py fixes now (2025-11-15 14:39:11.123 UTC) and the zone (US Pacific, as a POSIX
	// rule). The numbers were checked against Node, in America/Los_Angeles, from a line-by-line copy. The
	// texts are AS3's own formats, as recalled from avmplus (not checked against Flash): Node writes others.
	public class date {
		public function date() {
			var epoch:Date = new Date(0);
			trace(epoch.time, epoch.getTime(), epoch.valueOf());
			trace(epoch.getUTCFullYear(), epoch.getUTCMonth(), epoch.getUTCDate(), epoch.getUTCDay(), epoch.getUTCHours());
			trace(epoch.toUTCString());

			var d:Date = new Date(2024, 1, 29, 13, 5, 9, 250);
			trace(d.time);
			trace(d.fullYear, d.month, d.date, d.day, d.hours, d.minutes, d.seconds, d.milliseconds);
			trace(d.fullYearUTC, d.monthUTC, d.dateUTC, d.dayUTC, d.hoursUTC, d.minutesUTC, d.secondsUTC, d.millisecondsUTC);
			trace(d.getFullYear(), d.getMonth(), d.getDate(), d.getDay(), d.getHours(), d.getMinutes(), d.getSeconds(), d.getMilliseconds());
			trace(d.getUTCDate(), d.getUTCHours(), d.getUTCMinutes(), d.getUTCSeconds(), d.getUTCMilliseconds());
			trace(d.timezoneOffset, d.getTimezoneOffset());
			trace(d);
			trace(d.toString(), "|", String(d), "|", "at " + d);
			trace(d.toDateString(), "|", d.toTimeString(), "|", d.toUTCString());
			trace(d.toLocaleString(), "|", d.toLocaleDateString(), "|", d.toLocaleTimeString());
			var summer:Date = new Date(2024, 6, 4, 0, 30);
			trace(summer, summer.timezoneOffset, summer.toLocaleTimeString());

			trace(d.setMonth(11), d.month, d.date, d.timezoneOffset);
			trace(d.setMonth(0, 31), d.toDateString());
			trace(d.setFullYear(2025), d.setHours(25), d.date, d.hours);
			trace(d.setUTCHours(0, 0, 0, 0), d.hoursUTC, d.hours, d.date);
			d.date += 1;
			d.minutes++;
			d.secondsUTC = 61;
			trace(d.time, d.date, d.minutes, d.seconds);
			trace(d.setDate(0), d.month, d.date);
			trace(d.setMilliseconds(1500), d.seconds, d.milliseconds);
			trace(d.setMinutes(10, 20, 30), d.setSeconds(5), d.setUTCDate(15), d.setUTCMonth(3, 1), d.setUTCFullYear(2026, 0));
			trace(d.setUTCMinutes(1), d.setUTCSeconds(2, 3), d.setUTCMilliseconds(4), d.setTime(86400000), d.time);
			d.time = 172800000.9;
			trace(d.time, d.fullYearUTC);

			trace(d.setTime(NaN), d.fullYear, d.getDay(), d.timezoneOffset);
			trace(d, "|", d.toUTCString(), "|", d.toLocaleString());
			trace(d.setMonth(3), d.time);
			trace(d.setFullYear(2000), d.month, d.date, d.hours);

			trace(Date.UTC(2000, 0, 1), Date.UTC(99, 11, 31, 23, 59, 59, 999), Date.UTC(2024, 1));
			trace(new Date(99, 0).fullYear, new Date(2024, 13, 1).month, new Date(2024, 13, 1).fullYear);
			trace(new Date(8.64e15).time, new Date(8.64e15 + 1).time, new Date(-1).getUTCFullYear());
			trace(new Date(-62198755200000).getUTCFullYear(), new Date(-8.64e15).toUTCString());

			trace(Date.parse("Thu Feb 29 13:05:09 GMT-0800 2024"), Date.parse("Thu Feb 29 21:05:09 2024 UTC"));
			trace(Date.parse("2/29/2024"), Date.parse("2024/02/29 13:05:09"), Date.parse("Feb 29 2024 1:05:09 PM"));
			trace(Date.parse("Thu Feb 29 2024"), Date.parse("Feb 29 2024 13:05:09 GMT+0100"), Date.parse("29 Feb 2024 12:00 AM"));
			trace(Date.parse("not a date"), Date.parse(""));
			trace(Date.parse("2024-02-29")); // AS3's Date.parse does not read ISO 8601 (as recalled).
			trace(Date.parse(d.toString()) == d.time, Date.parse(new Date(2024, 4, 5, 6, 7, 8).toString()));
			trace(new Date("Feb 29 2024 13:05:09 GMT+0100").toUTCString(), new Date("x").time);

			var now:Date = new Date();
			trace(now.time, now);
			var copy:Date = new Date(now);
			trace(copy.time, copy == now);
			trace(new Date(null).time, new Date(undefined).time, new Date(true).time, new Date("1/2/2003").time);

			var u:* = now;
			var other:* = new Date(now.time);
			trace(typeof u, typeof now, u == now, u == other, u === now, now is Date, u is Date);
			trace(u, String(u) == String(now));

			// A Date where a Number is wanted is its time; beside `+` it is its text.
			var none:Date = null;
			trace(Number(epoch), new Date(5) - epoch, epoch < new Date(5), epoch >= new Date(5), Number(none));
			var w:* = new Date(10);
			var v:* = new Date(20);
			trace(w < v, v - w, w * 2, epoch + 1, w + 1);
			var n:Number = 5;
			n += epoch.time;
			var s:String = "at ";
			s += epoch;
			trace(n, s);
		}
	}
}
