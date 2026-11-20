package {
    import godot.*;

    public class Coin extends Area2D {
        [Export] public var gold:Boolean = false;
        private var age:Number = 0;

        override public function _process(delta:Number):void {
            age += delta;
            queueRedraw();
        }

        override public function _draw():void {
            var edge:Color = gold ? new Color("#b86433") : new Color("#247a88");
            var face:Color = gold ? new Color("#ffc847") : new Color("#72efdf");
            var shine:Color = gold ? new Color("#fff2b4") : new Color("#daffed");
            var y:Number = -3 + sin(age * 4) * 2;
            var half:Number = 9 + 3 * cos(age * 4);
            drawRect(new Rect2(-11, 10, 22, 3), new Color("#101e34bb"));
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
    }
}
