package dev.kumikae.lanternreach {
    import dev.kumikae.lanternreach.sim.*;
    import godot.CanvasLayer;
    import godot.FontVariation;
    import godot.Vector2;
    import godot.Transform2D;
    import godot.Control;
    import godot.PackedScene;
    import godot.ResourceLoader;
    import godot.Label;
    import godot.Button;
    import godot.TextureRect;
    import godot.Texture2D;
    import godot.AtlasTexture;
    import godot.Rect2;

    public class Desk extends CanvasLayer {
        public var words:Strings = new Strings();
        public var selected:int = -1;
        private var page:Control;
        private var titleLabel:Label;
        private var entry:Label;
        private var art:TextureRect;
        private var status:Label;
        private var notice:Label;
        private var buttons:Vector.<Button> = new Vector.<Button>();
        private var tools:Vector.<Button> = new Vector.<Button>();
        private var cards:Plate;
        private var bus:EventBus;
        private var listener:Subscription;
        private var wave:int;
        private var noticeUntil:int;
        private var shownTick:int;

        public function setup(game:Simulation):void {
            var scene:PackedScene = ResourceLoader.load("res://scenes/overlay.tscn") as PackedScene;
            var ui:Control = scene.instantiate() as Control;
            addChild(ui);
            page = ui.getNode("Page") as Control;
            titleLabel = ui.getNode("Page/Heading") as Label;
            entry = ui.getNode("Page/Entry") as Label;
            art = ui.getNode("Page/Art") as TextureRect;
            status = ui.getNode("Status") as Label;
            notice = ui.getNode("Notice") as Label;
            cards = new Plate("cards", 2, 4);
            for (var i:int = 0; i < 6; i++) {
                var choice:Button = ui.getNode("Page/Choice" + i) as Button;
                buttons.push(choice);
                bindChoice(choice, i);
            }
            for (var j:int = 0; j < 5; j++) {
                var tool:Button = ui.getNode("Tools/Slot" + j) as Button;
                tools.push(tool);
                bindChoice(tool, j);
            }
            bus = game.events;
            listener = bus.listen(receive);
        }

        private function bindChoice(button:Button, index:int):void {
            button.pressed.add(function():void { selected = index; });
        }

        public function close():void {
            if (bus != null) bus.remove(listener);
        }

        private function receive(item:WorldEvent):void {
            if (item.kind == WorldEvent.WAVE_START) wave = item.value;
            if (item.kind == WorldEvent.REFUSED) {
                notice.text = words.get("refused");
                noticeUntil = shownTick + 150;
            }
        }

        public function hud(game:Simulation, night:int, day:Boolean):void {
            shownTick = game.tick;
            if (shownTick >= noticeUntil) notice.text = "";
            status.text = words.get(day ? "day" : "night") + " " + night + "     " + words.get("beacons") +
                " " + game.grid.beacons.length + "     " + words.get("salvage") + " " + game.keeper.salvage +
                "     " + words.get("level") + " " + game.keeper.level + "     " + words.get("xp") +
                " " + game.keeper.xp + "     " + words.get("wave") + " " + wave;
        }

        public function sheet(heading:String, body:String, imageKey:String, frameIndex:int):void {
            page.visible = true;
            status.visible = false;
            entry.removeThemeFontOverride("font");
            titleLabel.text = heading;
            entry.text = body;
            if (imageKey == "cards") {
                var pic:AtlasTexture = new AtlasTexture();
                pic.atlas = cards.ink;
                var width:int = int(cards.ink.getWidth() / 2);
                var height:int = int(cards.ink.getHeight() / 4);
                pic.region = new Rect2(frameIndex % 2 * width, int(frameIndex / 2) * height, width, height);
                art.texture = pic;
            } else {
                art.texture = ResourceLoader.load("res://assets/art/" + imageKey + ".png") as Texture2D;
            }
            for each (var tool:Button in tools) tool.visible = false;
            for each (var choice:Button in buttons) choice.visible = false;
        }

        public function field():void {
            page.visible = false;
            status.visible = true;
            for each (var tool:Button in tools) tool.visible = false;
        }

        public function option(slot:int, caption:String):void {
            var button:Button = page.visible ? buttons[slot] : tools[slot];
            button.icon = null;
            button.text = caption;
            button.visible = true;
            if (page.visible && slot == 0) button.grabFocus();
        }
        public function handwriting(keeper:int):void {
            var face:FontVariation = new FontVariation();
            face.baseFont = entry.getThemeDefaultFont();
            face.variationEmbolden = keeper % 2 == 0 ? 0 : 0.25;
            face.variationTransform = new Transform2D(new Vector2(1, 0), new Vector2(keeper % 2 == 0 ? -0.15 : 0.08, 1), new Vector2());
            entry.addThemeFontOverride("font", face);
        }

        public function stamp(slot:int, id:int):void {
            var ink:Texture2D = ResourceLoader.load("res://assets/art/stamps.png") as Texture2D;
            var image:AtlasTexture = new AtlasTexture();
            image.atlas = ink;
            var side:int = int(ink.getWidth() / 5);
            image.region = new Rect2(id % 5 * side, int(id / 5) * side, side, side);
            buttons[slot].icon = image;
            buttons[slot].addThemeConstantOverride("icon_max_width", 28);
        }

    }
}
