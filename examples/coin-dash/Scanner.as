package {
    import godot.*;

    // The beam's extent is shared with collision so its tip and its danger zone agree.
    public class Scanner extends Node2D {
        public static const BEAM:Number = 102;
        private var age:Number = 0;

        override public function _process(delta:Number):void {
            age += delta;
            queueRedraw();
        }

        override public function _draw():void {
            var red:Color = new Color("#ff525e");
            var steel:Color = new Color("#293449");
            var amber:Color = new Color("#ffad79");
            drawRect(new Rect2(-9, 0, 18, BEAM), new Color("#ff40532a"));
            drawRect(new Rect2(-3, 0, 6, BEAM), red);
            drawRect(new Rect2(-1, 0, 2, BEAM), new Color("#fff1ce"));
            drawRect(new Rect2(-8, BEAM - 2, 16, 4), amber);
            var wing:Number = sin(age * 28) * 3;
            drawRect(new Rect2(-26, -7, 52, 8), steel);
            drawRect(new Rect2(-26 + wing, -10, 13, 4), amber);
            drawRect(new Rect2(13 - wing, -10, 13, 4), amber);
            drawRect(new Rect2(-12, -13, 24, 18), steel);
            drawRect(new Rect2(-9, -15, 18, 5), amber);
            drawRect(new Rect2(-6, -7, 12, 7), red);
            drawRect(new Rect2(-3, -6, 6, 3), Color.WHITE);
        }
    }
}
