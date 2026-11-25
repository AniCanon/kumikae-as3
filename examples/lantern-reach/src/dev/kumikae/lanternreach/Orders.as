package dev.kumikae.lanternreach {
    import dev.kumikae.lanternreach.sim.*;
    import godot.Node2D;
    import godot.Input;
    import godot.Sprite2D;
    import godot.Texture2D;
    import godot.ResourceLoader;
    import godot.OS;
    import godot.Vector2;
    import godot.InputEvent;
    import godot.InputEventScreenTouch;
    import godot.InputEventScreenDrag;
    import godot.InputEventMouseButton;

    public class Orders extends Node2D {
        public var active:Boolean;
        public var day:Boolean;
        public var tool:String = "lamp";
        private var game:Simulation;
        private var cursor:Sprite2D = new Sprite2D();
        private var tapped:Boolean;
        private var point:Vector2 = new Vector2();
        private var finger:int = -1;
        private var origin:Vector2 = new Vector2();
        private var drag:Vector2 = new Vector2();

        public function setup(sim:Simulation):void {
            game = sim;
            cursor.texture = ResourceLoader.load("res://assets/art/controls.png") as Texture2D;
            cursor.hframes = 3;
            cursor.vframes = 2;
            cursor.scale = new Vector2(0.3, 0.3);
            cursor.visible = false;
            addChild(cursor);
        }

        public function send(verb:String, x:Number, y:Number):void {
            game.commands.enqueue(new TickCommand(game.tick, verb, x, y));
        }

        override public function _unhandledInput(event:InputEvent):void {
            if (!active) return;
            var touch:InputEventScreenTouch = event as InputEventScreenTouch;
            if (touch != null) {
                if (touch.pressed && (day || touch.position.x < 480) && finger < 0) {
                    finger = touch.index;
                    origin = touch.position;
                    drag = new Vector2();
                } else if (!touch.pressed && touch.index == finger) {
                    if (day && drag.length() < 6) { point = touch.position; tapped = true; }
                    finger = -1;
                    drag = new Vector2();
                } else if (touch.pressed) {
                    aimAt(touch.position);
                    send("fire", 1, 0);
                }
            }
            var motion:InputEventScreenDrag = event as InputEventScreenDrag;
            if (motion != null && motion.index == finger) drag = (motion.position - origin).limitLength(60);
            var mouse:InputEventMouseButton = event as InputEventMouseButton;
            if (!OS.hasFeature("mobile") && mouse != null && mouse.pressed && mouse.buttonIndex == 1 && day) {
                point = mouse.position;
                tapped = true;
            }
        }

        private function aimAt(where:Vector2):void {
            var x:Number = where.x / 15 - game.keeper.x;
            var y:Number = where.y / 13 - game.keeper.y;
            if (x != 0 || y != 0) send("aim", x, y);
        }

        public function poll():void {
            cursor.position = getLocalMousePosition();
            cursor.visible = !OS.hasFeature("mobile");
            cursor.frame = day ? 5 : 4;
            var dir:Vector2 = Input.getVector("move_left", "move_right", "move_up", "move_down");
            if (finger >= 0) dir = drag / 60;
            send("move", dir.x, dir.y);
            var stick:Vector2 = Input.getVector("aim_left", "aim_right", "aim_up", "aim_down");
            if (day) {
                if (Input.isActionJustPressed("place")) {
                    point = new Vector2(game.keeper.x * 15, game.keeper.y * 13);
                    tapped = true;
                }
                if (tapped) {
                    send(tool, int(point.x / 15), int(point.y / 13));
                    tapped = false;
                }
            } else {
                if (stick.length() > 0) send("aim", stick.x, stick.y);
                else if (!OS.hasFeature("mobile")) aimAt(getLocalMousePosition());
                if (!OS.hasFeature("mobile")) send("fire", Input.isActionPressed("swing") || stick.length() > 0 ? 1 : 0, 0);
                if (Input.isActionJustPressed("dash")) send("dash", 0, 0);
            }
        }
    }
}
