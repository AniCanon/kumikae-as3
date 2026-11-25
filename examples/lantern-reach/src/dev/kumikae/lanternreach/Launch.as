package dev.kumikae.lanternreach {
    import godot.Node;
    import godot.OS;
    import godot.ProjectSettings;
    import godot.PackedScene;
    import godot.PackedStringArray;
    import godot.ResourceLoader;

    public class Launch extends Node {
        public static function option(key:String, fallback:String):String {
            var args:PackedStringArray = OS.getCmdlineUserArgs();
            for (var i:int = 0; i + 1 < args.size(); i++) {
                if (args.get(i) == key) return args.get(i + 1);
            }
            return fallback;
        }

        override public function _ready():void {
            var lang:String = option("--lang", String(ProjectSettings.getSetting("lantern/language", "as3")));
            if (lang != "cs" && lang != "as3") throw new ArgumentError("Unknown language " + lang);
            var scene:PackedScene = ResourceLoader.load("res://scenes/" + lang + ".tscn") as PackedScene;
            addChild(scene.instantiate());
        }
    }
}
