package dev.kumikae.coindash.presentation {
    import dev.kumikae.coindash.config.RoundRules;
    import dev.kumikae.coindash.gameplay.Round;
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
            summary.text = RoundRules.ROUND_SECONDS + " seconds. Dash through the laser for gold.";
            (menu.getNode("Brief") as Label).text = "Silver +" + RoundRules.SILVER_POINTS +
                "     Gold +" + RoundRules.GOLD_POINTS + "     Laser −" + RoundRules.HIT_PENALTY_SECONDS + " seconds";
            touch = OS.hasFeature("mobile");
            if (touch) {
                (root.getNode("Keys") as Label).text = "DRAG   MOVE      ·      TAP DASH   CROSS THE LASER";
                action.text = "START RUN";
            }
        }

        public function update(run:Round, best:int):void {
            score.text = digits(run.score);
            clock.text = digits(int(Math.ceil(run.seconds)));
            clock.modulate = run.seconds <= RoundRules.COUNTDOWN_WARNING_SECONDS ? warm : Color.WHITE;
            meter.size = new Vector2(132 * run.seconds / RoundRules.ROUND_SECONDS, 3);
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
            summary.text = run.gold + " gold collected   ·   " + run.silver + " silver collected";
            action.text = touch ? "RUN AGAIN" : "RUN AGAIN    [ R ]";
        }

        public function showSilver(point:Vector2):void {
            flash("+" + RoundRules.SILVER_POINTS, point, new Color("#abffea"));
        }

        public function showGold(point:Vector2):void {
            flash("+" + RoundRules.GOLD_POINTS + " GOLD", point, new Color("#ffe49a"));
        }

        public function showPenalty(point:Vector2):void {
            flash("−" + RoundRules.HIT_PENALTY_SECONDS + " SEC", point, new Color("#ffa18e"));
        }

        private function flash(message:String, point:Vector2, tint:Color):void {
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
