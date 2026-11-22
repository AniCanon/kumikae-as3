package dev.kumikae.coindash.entities {
    import dev.kumikae.coindash.presentation.Palette;
    import godot.CharacterBody2D;
    import godot.Color;
    import godot.Rect2;
    import godot.Vector2;
    import godot.sin;

    // Two-frame courier. The scarf, boot spacing and jet trail follow the real movement.
    public class Player extends CharacterBody2D {
        private static const DASH_SPEED:Number = 780;
        private static const AIM_DEAD_ZONE:Number = 0.1;
        private static const BLINK_RATE:Number = 12;

        private var heading:Vector2 = Vector2.RIGHT;
        private var dashHeading:Vector2 = Vector2.RIGHT;
        private var dashing:Boolean = false;
        private var blinking:Boolean = false;
        private var step:Number = 0;
        private var facing:int = 1;

        public function resetAt(dock:Vector2):void {
            position = dock;
            velocity = Vector2.ZERO;
            heading = Vector2.RIGHT;
            dashing = false;
            setBlinking(false);
        }

        public function drive(direction:Vector2, boost:Boolean, speed:Number, deck:Rect2):void {
            if (direction.length() > AIM_DEAD_ZONE) {
                heading = direction.normalized();
            }
            if (boost && !dashing) {
                dashHeading = heading;
            }
            dashing = boost;
            velocity = dashing ? dashHeading * DASH_SPEED : direction * speed;
            moveAndSlide();
            position = position.clamp(deck.position, deck.end);
        }

        public function showGrace(seconds:Number):void {
            setBlinking(seconds > 0 && int(seconds * BLINK_RATE) % 2 == 0);
        }

        public function clearGrace():void {
            setBlinking(false);
        }

        override public function _process(delta:Number):void {
            if (velocity.x > 5) {
                facing = 1;
            } else if (velocity.x < -5) {
                facing = -1;
            }
            step += delta * (velocity.length() > 20 ? 16 : 3);
            queueRedraw();
        }

        private function setBlinking(next:Boolean):void {
            if (blinking == next) {
                return;
            }
            blinking = next;
            modulate = blinking ? Palette.PLAYER_GRACE : Color.WHITE;
        }

        private var poses:[][]Rect2 = new [4] []Rect2;

        private function pixel(direction:int, x:int, y:int, w:int, h:int):Rect2 {
            return new Rect2(direction > 0 ? x : -x - w, y, w, h);
        }

        private function makePose(direction:int, lift:int):[]Rect2 {
            return new []Rect2 {
                pixel(direction, -10, -21 + lift, 17, 4),
                pixel(direction, -13, -17 + lift, 24, 14),
                pixel(direction, -11, -17 + lift, 20, 9),
                pixel(direction, -8, -20 + lift, 14, 5),
                pixel(direction, -2, -15 + lift, 14, 7),
                pixel(direction, 1, -14 + lift, 10, 3),
                pixel(direction, -10, -5, 18, 13),
                pixel(direction, -7, -4, 12, 10),
                pixel(direction, -8, -5, 16, 4),
                pixel(direction, -15, -5 + lift, 8, 3),
                pixel(direction, -20, -7 + lift * 2, 6, 3),
                pixel(direction, -12, 0, 5, 6),
                pixel(direction, -8 - lift * 2, 7 - lift, 7, 5),
                pixel(direction, 2 + lift * 2, 6 + lift, 7, 5)
            };
        }

        override public function _draw():void {
            var lift:int = sin(step) > 0 ? 1 : 0;
            drawRect(new Rect2(-12, 10, 24, 4), Palette.PLAYER_SHADOW);
            if (dashing && velocity.length() > 0) {
                var tail:Vector2 = velocity.normalized() * -36;
                drawLine(tail, Vector2.ZERO, Palette.PLAYER_SUIT, 7);
                drawLine(tail * 0.75, Vector2.ZERO, Palette.PLAYER_LIGHT, 3);
            }
            var index:int = (facing > 0 ? 0 : 2) + lift;
            var pose:[]Rect2 = poses[index];
            if (pose == null) {
                pose = makePose(facing, lift);
                poses[index] = pose;
            }
            drawRect(pose[0], Palette.PLAYER_INK);
            drawRect(pose[1], Palette.PLAYER_INK);
            drawRect(pose[2], Palette.PLAYER_SUIT);
            drawRect(pose[3], Palette.PLAYER_LIGHT);
            drawRect(pose[4], Palette.PLAYER_INK);
            drawRect(pose[5], Palette.PLAYER_LIGHT);
            drawRect(pose[6], Palette.PLAYER_INK);
            drawRect(pose[7], Palette.PLAYER_SUIT);
            drawRect(pose[8], Palette.PLAYER_SCARF);
            drawRect(pose[9], Palette.PLAYER_SCARF);
            drawRect(pose[10], Palette.PLAYER_SCARF);
            drawRect(pose[11], Palette.PLAYER_LIGHT);
            drawRect(pose[12], Palette.PLAYER_INK);
            drawRect(pose[13], Palette.PLAYER_INK);
        }
    }
}
