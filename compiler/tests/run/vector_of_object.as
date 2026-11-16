package {
	// A Vector.<Object> holds what an Object variable holds: never undefined. Its new places are null, and
	// undefined stored into it is null, however it is stored. A Vector.<*> keeps undefined.
	public class vector_of_object {
		public function vector_of_object() {
			var nothing:*;
			var made:Vector.<Object> = new Vector.<Object>(2);
			trace(made[0] === null, made[1] === undefined);
			made.length = 4;
			trace(made[3] === null);
			made[0] = nothing;
			made.push(nothing, undefined);
			made.unshift(nothing);
			made.insertAt(1, nothing);
			made.splice(2, 0, nothing, 7);
			trace(count(made), made.length, made[3]);
			var written:Vector.<Object> = new <Object>[nothing, 1, undefined];
			trace(count(written), written.length);
			var mapped:Vector.<Object> = written.map(function (item:Object, index:int, all:Vector.<Object>):* {
				return undefined;
			});
			trace(count(mapped), mapped.length);
			var held:* = written;
			held.push(undefined);
			held[0] = undefined;
			held.unshift(nothing);
			trace(count(written), written.length);
			var from:Vector.<Object> = Vector.<Object>([nothing, 2]);
			trace(count(from), from.concat(written).length, count(from.concat(written)));
			var fixed:Vector.<Object> = new Vector.<Object>(3, true);
			trace(count(fixed), fixed.indexOf(null), fixed.indexOf(undefined), made.lastIndexOf(nothing) >= 0);
			// A Vector.<*> and an Array hold undefined as it is.
			var any:Vector.<*> = new Vector.<*>(2);
			any.push(nothing);
			trace(any[0] === undefined, any[2] === undefined, [nothing][0] === undefined);
		}
		// How many of its places are undefined, which none may be.
		private function count(p_list:Vector.<Object>):int {
			var found:int = 0;
			for (var i:int = 0; i < p_list.length; i++) {
				if (p_list[i] === undefined) {
					found++;
				}
			}
			return found;
		}
	}
}
