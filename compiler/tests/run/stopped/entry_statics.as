package {
    // The loop is in what gives the entry class's static fields their values, and asks the library each time
    // round: the class is set up in a call like any other, whose time is kept.
    public class entry_statics {
        private static var turns:int = spin();

        public function entry_statics() {
            trace("made", turns);
        }

        private static function spin():int {
            var list:Array = [1, 2];
            var held:Object = { count: 0 };
            try { while (true) { held.count = JSON.stringify(list).length + held.count; } } catch (error:*) { trace("caught"); } finally { trace("finally"); }
            return 1;
        }
    }
}
