package {
    import godot.*;

    public class Hud {
        private var score:Label;
        private var clock:Label;
        private var record:Label;
        private var meter:ColorRect;
        private var menu:Control;
        private var eyebrow:Label;
        private var title:Label;
        private var summary:Label;
        private var action:Button;
        private var burst:Button;
        private var popup:Label;
        private var touch:Boolean;
        private var fade:Number = 0;
        private var warm:Color = new Color("#ff8864");

        public function Hud(root:Control) {
            score = root.getNode("Score") as Label;
            clock = root.getNode("Clock") as Label;
            record = root.getNode("Record") as Label;
            meter = root.getNode("TimeBar") as ColorRect;
            menu = root.getNode("Menu") as Control;
            eyebrow = menu.getNode("Eyebrow") as Label;
            title = menu.getNode("Title") as Label;
            summary = menu.getNode("Summary") as Label;
            action = menu.getNode("Action") as Button;
            burst = root.getNode("Burst") as Button;
            popup = root.getNode("Popup") as Label;
            touch = OS.hasFeature("mobile");
            if (touch) {
                (root.getNode("Keys") as Label).text = "DRAG   MOVE      ·      TAP DASH   CROSS THE LASER";
                action.text = "START RUN";
            }
        }

        public function update(run:Round, best:int):void {
            score.text = digits(run.score);
            clock.text = digits(int(Math.ceil(run.seconds)));
            clock.modulate = run.seconds <= 5 ? warm : Color.WHITE;
            meter.size = new Vector2(132 * run.seconds / Round.LENGTH, 3);
            record.text = digits(best);
            burst.text = run.recharge > 0 ? "RECHARGING" : "DASH  ›";
        }

        public function playing():void {
            menu.visible = false;
            burst.visible = true;
            fade = 0;
            popup.visible = false;
        }

        public function finished(run:Round, newRecord:Boolean):void {
            burst.visible = false;
            popup.visible = false;
            fade = 0;
            menu.visible = true;
            eyebrow.text = newRecord ? "DISPATCH COMPLETE    /    NEW RECORD" : "DISPATCH COMPLETE    /    TIME UP";
            title.text = digits(run.score) + " POINTS";
            summary.text = run.gold + " gold collected   ·   " + (run.score - run.gold * 3) + " silver collected";
            action.text = touch ? "RUN AGAIN" : "RUN AGAIN    [ R ]";
        }

        public function flash(message:String, point:Vector2, tint:Color):void {
            popup.text = message;
            popup.position = new Vector2(point.x - 70, point.y - 49);
            popup.modulate = tint;
            popup.visible = true;
            fade = 0.62;
        }

        public function animate(delta:Number):void {
            if (fade <= 0) return;
            fade -= delta;
            popup.position = popup.position + new Vector2(0, -22 * delta);
            popup.visible = fade > 0;
        }

        private function digits(n:int):String {
            return n < 10 ? "0" + n : String(n);
        }
    }
}
