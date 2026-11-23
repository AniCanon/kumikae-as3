package {
    public class dynamic_local_tags {
        public function dynamic_local_tags() {
            probe(7);
            trace(branch(false), branch(true));
            trace(failedWrite());
            trace(closureWrite());
            try { trace(nullableText(null)); } catch (e:TypeError) { trace(e.errorID); }
        }
        private function nullableText(source:String):int { var value:* = source; return value.length; }
        private function branch(take:Boolean):String {
            if (take) { var value:* = 7; }
            return value === undefined ? "unwritten" : String(value);
        }
        private function raises():int { throw new Error("before write"); }
        private function failedWrite():String {
            try { var value:* = raises(); }
            catch (e:Error) { return value === undefined ? "unwritten" : "written"; }
            return "returned";
        }
        private function closureWrite():String {
            var captured:* = 7;
            var mutate:Function = function():void { captured = "changed"; };
            mutate();
            return captured;
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
