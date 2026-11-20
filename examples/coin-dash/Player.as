package {
    import godot.*;

    // Two-frame courier. The scarf, boot spacing and jet trail follow the real movement.
    public class Player extends CharacterBody2D {
        private var step:Number = 0;
        private var facing:int = 1;
        private var ink:Color = new Color("#142239");
        private var suit:Color = new Color("#36bab4");
        private var light:Color = new Color("#9affdf");
        private var scarf:Color = new Color("#ff8456");

        override public function _process(delta:Number):void {
            if (velocity.x > 5) facing = 1;
            if (velocity.x < -5) facing = -1;
            step += delta * (velocity.length() > 20 ? 16 : 3);
            queueRedraw();
        }

        private function pixel(x:Number, y:Number, w:Number, h:Number, color:Color):void {
            drawRect(new Rect2(facing > 0 ? x : -x - w, y, w, h), color);
        }

        override public function _draw():void {
            var lift:int = sin(step) > 0 ? 1 : 0;
            drawRect(new Rect2(-12, 10, 24, 4), new Color("#10273dbb"));
            if (velocity.length() > 550) {
                var tail:Vector2 = velocity.normalized() * -36;
                drawLine(tail, Vector2.ZERO, suit, 7);
                drawLine(tail * 0.75, Vector2.ZERO, light, 3);
            }
            pixel(-10, -21 + lift, 17, 4, ink);
            pixel(-13, -17 + lift, 24, 14, ink);
            pixel(-11, -17 + lift, 20, 9, suit);
            pixel(-8, -20 + lift, 14, 5, light);
            pixel(-2, -15 + lift, 14, 7, ink);
            pixel(1, -14 + lift, 10, 3, light);
            pixel(-10, -5, 18, 13, ink);
            pixel(-7, -4, 12, 10, suit);
            pixel(-8, -5, 16, 4, scarf);
            pixel(-15, -5 + lift, 8, 3, scarf);
            pixel(-20, -7 + lift * 2, 6, 3, scarf);
            pixel(-12, 0, 5, 6, light);
            pixel(-8 - lift * 2, 7 - lift, 7, 5, ink);
            pixel(2 + lift * 2, 6 + lift, 7, 5, ink);
        }
    }
}
