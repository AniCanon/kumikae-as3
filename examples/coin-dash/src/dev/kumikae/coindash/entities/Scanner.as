package dev.kumikae.coindash.entities {
    import dev.kumikae.coindash.presentation.Palette;
    import godot.Color;
    import godot.Node2D;
    import godot.Rect2;
    import godot.Vector2;
    import godot.absf;
    import godot.sin;

    // The beam's extent is shared with collision so its tip and its danger zone agree.
    public class Scanner extends Node2D {
        private static const BEAM:Number = 102;
        private static const HIT_HALF_WIDTH:Number = 16;
        private static const HIT_ABOVE:Number = 19;
        private static const HIT_BELOW:Number = 11;
        private static const SWEEP_RADIUS:Number = 331;
        private static const SWEEP_RATE:Number = 1.50;
        private static const SPEEDUP_AT_SECONDS:Number = 20;
        private static const EXTRA_SWEEP_RATE:Number = 0.38;

        private var origin:Vector2 = Vector2.ZERO;
        private var patrolTime:Number = 0;
        private var age:Number = 0;

        override public function _ready():void {
            origin = position;
        }

        public function sweep(delta:Number):void {
            patrolTime += delta;
            var fast:Number = Math.max(0, patrolTime - SPEEDUP_AT_SECONDS);
            var phase:Number = patrolTime * SWEEP_RATE + fast * EXTRA_SWEEP_RATE;
            position = origin + new Vector2(SWEEP_RADIUS * sin(phase), 0);
        }

        public function resetPatrol():void {
            patrolTime = 0;
            position = origin;
        }

        public function covers(point:Vector2):Boolean {
            var offset:Vector2 = point - position;
            return absf(offset.x) < HIT_HALF_WIDTH && offset.y > -HIT_ABOVE && offset.y < BEAM + HIT_BELOW;
        }

        override public function _process(delta:Number):void {
            age += delta;
            queueRedraw();
        }

        override public function _draw():void {
            drawRect(new Rect2(-9, 0, 18, BEAM), Palette.SCANNER_BEAM_GLOW);
            drawRect(new Rect2(-3, 0, 6, BEAM), Palette.SCANNER_RED);
            drawRect(new Rect2(-1, 0, 2, BEAM), Palette.SCANNER_BEAM_CORE);
            drawRect(new Rect2(-8, BEAM - 2, 16, 4), Palette.SCANNER_AMBER);
            var wing:Number = sin(age * 28) * 3;
            drawRect(new Rect2(-26, -7, 52, 8), Palette.SCANNER_STEEL);
            drawRect(new Rect2(-26 + wing, -10, 13, 4), Palette.SCANNER_AMBER);
            drawRect(new Rect2(13 - wing, -10, 13, 4), Palette.SCANNER_AMBER);
            drawRect(new Rect2(-12, -13, 24, 18), Palette.SCANNER_STEEL);
            drawRect(new Rect2(-9, -15, 18, 5), Palette.SCANNER_AMBER);
            drawRect(new Rect2(-6, -7, 12, 7), Palette.SCANNER_RED);
            drawRect(new Rect2(-3, -6, 6, 3), Color.WHITE);
        }
    }
}
