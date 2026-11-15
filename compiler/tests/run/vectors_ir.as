package {
	public class vectors_ir {
		public function vectors_ir() {
			var cells:Vector.<int> = new <int>[1, 2, 3];
			var i:int = 1;
			cells[i] = cells[0] + 5;
			cells.push(9);
			var total:int = 0;
			for each (var cell:int in cells) {
				total += cell;
			}
			trace(cells.length, total);
		}
	}
}
