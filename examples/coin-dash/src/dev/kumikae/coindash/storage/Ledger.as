package dev.kumikae.coindash.storage {
    import godot.*;

    public class Ledger {
        private static const PATH:String = "user://coin-dash-record.cfg";
        private static const SECTION:String = "rooftop";
        private static const BEST:String = "best";

        public static function loadBest():int {
            var file:ConfigFile = new ConfigFile();
            if (!FileAccess.fileExists(PATH) || file.load(PATH) != 0) return 0;
            return Math.max(0, int(file.getValue(SECTION, BEST, 0)));
        }

        public static function saveBest(points:int):void {
            var file:ConfigFile = new ConfigFile();
            file.setValue(SECTION, BEST, points);
            if (file.save(PATH) != 0) pushWarning("Coin Dash could not save the local record.");
        }
    }
}
