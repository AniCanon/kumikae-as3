package {
    public class typed_iteration {
        public function typed_iteration() {
            var data:Object = JSON.parse('{"rows":[[2],[3]]}');
            for each (var row:Array in data.rows) trace(row[0]);
            var values:Vector.<*> = new Vector.<*>();
            values.push(new <int>[5], null, "wrong");
            try {
                for each (var item:Vector.<int> in values) trace(item);
            } catch (error:TypeError) {
                trace(error.errorID);
            }
        }
    }
}
