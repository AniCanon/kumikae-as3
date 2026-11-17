package {
	// A vector of ints or of uints keeps each number in four bytes, where every other vector's place is
	// eight: every way such a vector is read, written, made longer or shorter, searched, sorted, copied and
	// put into words is held here to what it gives, with numbers that only fit if all thirty-two bits are
	// kept and none past them are read (a uint above 2^31, an int below zero, a place past half the vector).
	public class vector_narrow {
		private var counts:Vector.<int> = new Vector.<int>();
		private var marks:Vector.<uint> = new Vector.<uint>(5, true);

		public function vector_narrow() {
			var i:int;
			for (i = 0; i < 40; i++) {
				counts[i] = i * 100000000;
			}
			trace(counts.length, counts[0], counts[21], counts[22], counts[39], counts[counts.length - 1] + counts[1]);
			counts[7] = 2147483647;
			counts[7]++;
			counts[8] = -2147483648;
			counts[8]--;
			trace(counts[7], counts[8], counts[6], counts[9]);
			marks[0] = 4294967295;
			marks[1] = 2147483648;
			marks[2] = -1;
			marks[3] = 3000000000 + 3000000000;
			marks[4]--;
			trace(marks, marks[0] + 1, marks[1] > 0, marks[4]);

			var words:Vector.<int> = new <int>[5, -3, 12, 0, -3, 99];
			trace(words.push(7, 8), words.pop(), words.shift(), words.unshift(-1, -2), words);
			trace(words.splice(2, 3, 41, 42), words, words.indexOf(-3), words.lastIndexOf(42), words.indexOf(1000));
			trace(words.reverse(), words.slice(1, 4), words.concat(new <int>[1, 2], new <int>[3]), words.join("+"));
			words.insertAt(1, -100);
			trace(words.removeAt(0), words, words.sort(function(a:int, b:int):int { return a - b; }));
			var large:Vector.<uint> = new <uint>[5, 4000000000, 3, 2147483648, 0];
			trace(large.sort(Array.NUMERIC), large.sort(Array.NUMERIC | Array.DESCENDING), large.concat(marks).length);
			words.length = 3;
			trace(words, words.length);
			words.length = 6;
			trace(words, words.map(function(x:int, at:int, all:Vector.<int>):int { return x * 2 + at; }),
					words.filter(function(x:int, at:int, all:Vector.<int>):Boolean { return x != 0; }),
					words.some(function(x:int, at:int, all:Vector.<int>):Boolean { return x > 40; }),
					words.every(function(x:int, at:int, all:Vector.<int>):Boolean { return x > 40; }));
			var total:Number = 0;
			words.forEach(function(x:int, at:int, all:Vector.<int>):void { total += x; });
			for each (var each:int in words) {
				total += each;
			}
			for (var key:String in words) {
				total += int(key);
			}
			trace(total);

			var untyped:* = large;
			untyped[1] = 4294967294;
			untyped.push(77);
			trace(untyped[1], untyped[0], untyped.length, large[5], untyped.indexOf(77), String(untyped));
			var any:Object = counts;
			trace(any[7], any["8"], any.length, JSON.stringify(large), JSON.stringify(words));
			var copy:Vector.<int> = Vector.<int>([1.9, "12", -7, 4294967295, true]);
			var wide:Vector.<Number> = Vector.<Number>(copy);
			var back:Vector.<uint> = Vector.<uint>(wide);
			trace(copy, wide, back, back[2]);
			// A vector of ints is never a vector of anything else: seen through an untyped value it converts to
			// a new vector, and is refused where one of another type is declared.
			var seen:Object = copy;
			var other:Vector.<Number> = seen as Vector.<Number>;
			var same:Vector.<int> = seen as Vector.<int>;
			var made:Vector.<Number> = Vector.<Number>(seen);
			made[4] = 0.5;
			trace(other == null, same === copy, made === seen, made, copy);
			try {
				var loose:* = copy;
				var wrong:Vector.<Number> = loose;
				trace(wrong);
			} catch (refused:Error) {
				trace(refused.message);
			}
			var nested:Vector.<Vector.<int>> = new Vector.<Vector.<int>>();
			nested.push(copy, counts.slice(38), new <int>[]);
			trace(nested, nested[1][1], nested[2].length);

			trace(sieve(100), sieve(100), filled(1000), summed(counts));
			try {
				trace(counts[40]);
			} catch (e:Error) {
				trace(e.message);
			}
			try {
				marks[5] = 1;
			} catch (e:Error) {
				trace(e.message);
			}
			try {
				marks.push(1);
			} catch (e:Error) {
				trace(e.message);
			}
		}

		// The loops native code writes with the vector's length and places in locals.
		private function sieve(limit:int):int {
			var flags:Vector.<int> = new Vector.<int>(limit, true);
			var i:int;
			for (i = 0; i < limit; i++) {
				flags[i] = 1;
			}
			var count:int = 0;
			for (i = 2; i < limit; i++) {
				if (flags[i] == 1) {
					count++;
					var j:int = i + i;
					while (j < limit) {
						flags[j] = 0;
						j += i;
					}
				}
			}
			return count;
		}

		// A vector that grows in the loop that fills it, read back from its far end.
		private function filled(n:int):Number {
			var made:Vector.<uint> = new Vector.<uint>();
			for (var i:int = 0; i < n; i++) {
				made[i] = i * 4294967;
			}
			var sum:Number = 0;
			for (i = n - 1; i >= 0; i--) {
				sum += made[i];
			}
			return sum + made.length;
		}

		private function summed(from:Vector.<int>):Number {
			var sum:Number = 0;
			var n:int = from.length;
			for (var i:int = 0; i < n; i++) {
				sum += from[i];
				from[i] = from[i] + 1;
			}
			return sum;
		}
	}
}
