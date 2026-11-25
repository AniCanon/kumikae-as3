package dev.kumikae.lanternreach {
    import godot.FileAccess;

    public final class Strings {
        private var entries:Object = JSON.parse(FileAccess.getFileAsString("res://data/strings.json"));

        public function get(key:String):String {
            return String(entries[key]);
        }
    }
}
