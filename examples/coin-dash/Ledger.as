package {
    import godot.*;

    public class Ledger {
        private static const PATH:String = "user://coin-dash-record.cfg";

        public static function loadBest():int {
            var file:ConfigFile = new ConfigFile();
            if (!FileAccess.fileExists(PATH) || file.load(PATH) != 0) return 0;
            return Math.max(0, int(file.getValue("rooftop", "best", 0)));
        }

        public static function saveBest(points:int):void {
            var file:ConfigFile = new ConfigFile();
            file.setValue("rooftop", "best", points);
            if (file.save(PATH) != 0) pushWarning("Coin Dash could not save the local record.");
        }
    }
}
