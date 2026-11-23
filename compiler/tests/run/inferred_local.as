package {
    public class inferred_local {
        public function inferred_local() {
            probe(7);
        }
        private function probe(source:int):void {
            var exact = source;
            var explicit:* = source;
            explicit = source;
            trace(typeof exact, exact is int, exact as String, explicit, arguments[0]);
            trace(early === undefined);
            var early:* = source;
            var mixed:* = source;
            mixed = null;
            trace(early, mixed === null, mixed === undefined);
            var captured:* = source;
            var change:Function = function():void { captured = undefined; };
            change();
            trace(captured === undefined);
            var key:* = "x";
            var item:Object = {x: exact};
            trace(delete item[key], key in item);
        }
    }
}
