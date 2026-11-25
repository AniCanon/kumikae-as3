package dev.kumikae.lanternreach {
    import dev.kumikae.lanternreach.sim.*;
    import godot.Node2D;
    import godot.Sprite2D;
    import godot.Label;
    import godot.Color;
    import godot.Rect2;
    import godot.Vector2;

    public class Impressions extends Node2D {
        private var bodies:Vector.<Sprite2D> = new Vector.<Sprite2D>();
        private var numbers:Vector.<Label> = new Vector.<Label>();
        private var remaining:Vector.<int> = new Vector.<int>();
        private var deaths:Vector.<Plate> = new Vector.<Plate>();
        private var signs:Plate;
        private var cursor:int;
        private var width:int;
        private var impact:int;
        private var listener:Subscription;
        public var shake:Boolean = true;

        public function setup(game:Simulation):void {
            width = game.grid.width;
            signs = new Plate("effects", 3, 3);
            for each (var def:EnemyDef in game.rules.enemies) {
                deaths.push(new Plate(def.key, def.key == "undertow" ? 4 : 3, 2));
            }
            for (var i:int = 0; i < 64; i++) {
                var body:Sprite2D = new Sprite2D();
                var number:Label = new Label();
                body.regionEnabled = true;
                body.visible = false;
                number.visible = false;
                number.addThemeFontSizeOverride("font_size", 12);
                number.addThemeConstantOverride("outline_size", 3);
                addChild(body);
                body.addChild(number);
                bodies.push(body);
                numbers.push(number);
                remaining.push(0);
            }
            listener = game.events.listen(receive);
        }

        public function close(bus:EventBus):void { bus.remove(listener); }

        private function receive(item:WorldEvent):void {
            if (item.kind != WorldEvent.DEATH && item.kind != WorldEvent.DAMAGE &&
                    item.kind != WorldEvent.FIRED && item.kind != WorldEvent.SWING && item.kind != WorldEvent.BEACON_LOST) return;
            var plate:Plate = item.kind == WorldEvent.DEATH ? deaths[item.value] : signs;
            var frameIndex:int = item.kind == WorldEvent.DEATH ? (item.value == Creature.UNDERTOW ? 0 : 5) :
                item.kind == WorldEvent.FIRED ? (item.value == Structure.BELL ? 3 : item.value == Structure.BRAZIER ? 2 : 1) :
                item.kind == WorldEvent.SWING ? 7 : 8;
            var body:Sprite2D = bodies[cursor];
            var w:int = int(plate.ink.getWidth() / plate.columns);
            var h:int = int(plate.ink.getHeight() / plate.rows);
            body.texture = plate.ink;
            body.regionRect = new Rect2(frameIndex % plate.columns * w, int(frameIndex / plate.columns) * h, w, h);
            body.scale = new Vector2(0.5, 0.5);
            body.position = new Vector2((item.cell % width + 0.5) * 15, (int(item.cell / width) + 0.5) * 13);
            body.visible = true;
            var number:Label = numbers[cursor];
            number.visible = item.kind == WorldEvent.DAMAGE;
            if (number.visible) {
                number.text = String(item.value);
                number.scale = new Vector2(2, 2);
            }
            if (item.kind == WorldEvent.BEACON_LOST || item.kind == WorldEvent.DAMAGE) impact = 4;
            remaining[cursor] = 14;
            cursor = (cursor + 1) % bodies.length;
        }

        public function present(view:Node2D):void {
            for (var i:int = 0; i < remaining.length; i++) {
                if (remaining[i] <= 0) continue;
                remaining[i]--;
                var body:Sprite2D = bodies[i];
                body.visible = remaining[i] > 0;
                body.modulate = new Color(1, 1, 1, remaining[i] / 14.0);
                if (numbers[i].visible) body.position = body.position + new Vector2(0, -0.5);
            }
            view.position = new Vector2(shake && impact > 0 ? impact % 2 * 2 - 1 : 0, 0);
            if (impact > 0) impact--;
        }
    }
}
