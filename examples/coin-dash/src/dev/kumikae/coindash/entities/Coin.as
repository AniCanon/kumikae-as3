package dev.kumikae.coindash.entities {
    import dev.kumikae.coindash.presentation.Palette;
    import godot.Area2D;
    import godot.Color;
    import godot.Engine;
    import godot.Node2D;
    import godot.Rect2;
    import godot.Signal;
    import godot.cos;
    import godot.sin;

    public class Coin extends Area2D {
        [Signal] public var collected:Signal;
        [Export] public var gold:Boolean = false;

        private var age:Number = 0;

        override public function _ready():void {
            if (Engine.isEditorHint()) {
                return;
            }
            bodyEntered.add(onBodyEntered);
        }

        override public function _process(delta:Number):void {
            age += delta;
            queueRedraw();
        }

        override public function _draw():void {
            var edge:Color = gold ? Palette.GOLD_EDGE : Palette.SILVER_EDGE;
            var face:Color = gold ? Palette.GOLD_FACE : Palette.SILVER_FACE;
            var shine:Color = gold ? Palette.GOLD_SHINE : Palette.SILVER_SHINE;
            var y:Number = -3 + sin(age * 4) * 2;
            var half:Number = 9 + 3 * cos(age * 4);
            drawRect(new Rect2(-11, 10, 22, 3), Palette.COIN_SHADOW);
            drawRect(new Rect2(-half + 3, y - 12, half * 2 - 6, 24), edge);
            drawRect(new Rect2(-half, y - 8, half * 2, 16), edge);
            drawRect(new Rect2(-half + 2, y - 8, half * 2 - 4, 12), face);
            drawRect(new Rect2(-half + 4, y - 11, half * 2 - 8, 3), shine);
            drawRect(new Rect2(-2, y - 5, 4, 10), shine);
            if (gold) {
                drawRect(new Rect2(-5, y - 2, 10, 4), shine);
                var spark:Number = 2 + sin(age * 6);
                drawRect(new Rect2(13, y - 14, 2, spark * 2), shine);
                drawRect(new Rect2(14 - spark, y - 12, spark * 2, 2), shine);
            }
        }

        private function onBodyEntered(body:Node2D):void {
            if (body is Player) {
                collected.emit();
            }
        }
    }
}
