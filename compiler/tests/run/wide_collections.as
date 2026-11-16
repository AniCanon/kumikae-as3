package { public class wide_collections {
	public function wide_collections() {
		var big:Int64 = Int64.MAX_VALUE;
		var v:Vector.<Int64> = new <Int64>[big, Int64(5)];
		v.push(Int64.MIN_VALUE);
		trace(v.length, v, v[0] == big, v.indexOf(Int64(5)));
		var a:Array = [big, 1];
		trace(a, a[0] == big, typeof a[0]);
		var back:Int64 = a[0];
		trace(back);
		trace(v.sort(Array.NUMERIC | Array.DESCENDING));
		// Past 2^53, where a Number can't tell them apart: by their own 64 bits.
		var near:Vector.<Int64> = new <Int64>[Int64.MAX_VALUE, Int64.MAX_VALUE - 1];
		trace(near.sort(Array.NUMERIC));
	}
} }
