package dev.kumikae.lanternreach {
    import dev.kumikae.lanternreach.sim.*;
    import godot.Texture2D;
    import godot.ResourceLoader;
    import godot.RenderingServer;
    import godot.Rect2;
    import godot.RID;

    public final class Plate {
        public var ink:Texture2D;
        public var columns:int;
        public var rows:int;
        private var width:int;
        private var height:int;

        public function Plate(name:String, across:int, down:int) {
            ink = ResourceLoader.load("res://assets/art/" + name + ".png") as Texture2D;
            columns = across;
            rows = down;
            width = int(ink.getWidth() / columns);
            height = int(ink.getHeight() / rows);
        }

        public function paint(canvas:RID, index:int, x:Number, y:Number, w:Number, h:Number):void {
            var src:Rect2 = new Rect2(index % columns * width, int(index / columns) * height, width, height);
            var dest:Rect2 = new Rect2(x - w * 0.5, y - h * 0.5, w, h);
            RenderingServer.canvasItemAddTextureRectRegion(canvas, dest, ink.getRID(), src);
        }
    }
}
