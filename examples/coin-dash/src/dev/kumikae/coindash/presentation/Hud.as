package dev.kumikae.coindash.presentation {
    import dev.kumikae.coindash.config.RoundRules;
    import dev.kumikae.coindash.gameplay.Round;
    import godot.Button;
    import godot.CanvasLayer;
    import godot.Color;
    import godot.ColorRect;
    import godot.Control;
    import godot.Engine;
    import godot.Label;
    import godot.OS;
    import godot.Signal;
    import godot.Vector2;

    public class Hud extends CanvasLayer {
        private static const METER_WIDTH:Number = 132;
        private static const METER_HEIGHT:Number = 3;
        private static const POPUP_DURATION:Number = 0.62;
        private static const POPUP_DRIFT_SPEED:Number = -22;

        [Signal] public var startRequested:Signal;
        [Signal] public var dashRequested:Signal;

        [Export] public var scoreLabel:Label;
        [Export] public var clockLabel:Label;
        [Export] public var recordLabel:Label;
        [Export] public var timeBar:ColorRect;
        [Export] public var menuPanel:Control;
        [Export] public var eyebrowLabel:Label;
        [Export] public var titleLabel:Label;
        [Export] public var summaryLabel:Label;
        [Export] public var briefLabel:Label;
        [Export] public var actionButton:Button;
        [Export] public var burstButton:Button;
        [Export] public var keysLabel:Label;
        [Export] public var popupLabel:Label;

        private var touch:Boolean = false;
        private var fade:Number = 0;
        private var lastScore:int = -1;
        private var lastSecond:int = -1;
        private var lastBest:int = -1;
        private var lastWarning:int = -1;
        private var lastRecharging:int = -1;
        private var lastMeterWidth:Number = -1;

        override public function _ready():void {
            if (Engine.isEditorHint()) {
                return;
            }
            summaryLabel.text = RoundRules.ROUND_SECONDS + " seconds. Dash through the laser for gold.";
            briefLabel.text = "Silver +" + RoundRules.SILVER_POINTS +
                "     Gold +" + RoundRules.GOLD_POINTS + "     Laser −" + RoundRules.HIT_PENALTY_SECONDS + " seconds";
            touch = OS.hasFeature("mobile");
            if (touch) {
                keysLabel.text = "DRAG   MOVE      ·      TAP DASH   CROSS THE LASER";
                actionButton.text = "START RUN";
            }
            actionButton.pressed.add(onStartPressed);
            burstButton.pressed.add(onDashPressed);
        }

        public function scoreText():String {
            return scoreLabel.text;
        }

        public function isMenuVisible():Boolean {
            return menuPanel.visible;
        }

        public function update(run:Round, best:int):void {
            if (run.score != lastScore) {
                lastScore = run.score;
                scoreLabel.text = digits(run.score);
            }
            var wholeSeconds:int = int(Math.ceil(run.seconds));
            if (wholeSeconds != lastSecond) {
                lastSecond = wholeSeconds;
                clockLabel.text = digits(wholeSeconds);
            }
            var warning:int = run.seconds <= RoundRules.COUNTDOWN_WARNING_SECONDS ? 1 : 0;
            if (warning != lastWarning) {
                lastWarning = warning;
                clockLabel.modulate = warning == 1 ? Palette.HUD_WARNING : Color.WHITE;
            }
            var meterWidth:Number = METER_WIDTH * run.seconds / RoundRules.ROUND_SECONDS;
            if (meterWidth != lastMeterWidth) {
                lastMeterWidth = meterWidth;
                timeBar.size = new Vector2(meterWidth, METER_HEIGHT);
            }
            if (best != lastBest) {
                lastBest = best;
                recordLabel.text = digits(best);
            }
            var recharging:int = run.recharge > 0 ? 1 : 0;
            if (recharging != lastRecharging) {
                lastRecharging = recharging;
                burstButton.text = recharging == 1 ? "RECHARGING" : "DASH  ›";
            }
        }

        public function playing():void {
            menuPanel.visible = false;
            burstButton.visible = true;
            fade = 0;
            popupLabel.visible = false;
        }

        public function finished(run:Round, newRecord:Boolean):void {
            burstButton.visible = false;
            popupLabel.visible = false;
            fade = 0;
            menuPanel.visible = true;
            eyebrowLabel.text = newRecord ? "DISPATCH COMPLETE    /    NEW RECORD" : "DISPATCH COMPLETE    /    TIME UP";
            titleLabel.text = digits(run.score) + " POINTS";
            summaryLabel.text = run.gold + " gold collected   ·   " + run.silver + " silver collected";
            actionButton.text = touch ? "RUN AGAIN" : "RUN AGAIN    [ R ]";
        }

        public function showSilver(point:Vector2):void {
            flash("+" + RoundRules.SILVER_POINTS, point, Palette.POPUP_SILVER);
        }

        public function showGold(point:Vector2):void {
            flash("+" + RoundRules.GOLD_POINTS + " GOLD", point, Palette.POPUP_GOLD);
        }

        public function showPenalty(point:Vector2):void {
            flash("−" + RoundRules.HIT_PENALTY_SECONDS + " SEC", point, Palette.POPUP_PENALTY);
        }

        public function animate(delta:Number):void {
            if (fade <= 0) {
                return;
            }
            fade -= delta;
            popupLabel.position = popupLabel.position + new Vector2(0, POPUP_DRIFT_SPEED * delta);
            if (fade <= 0) {
                popupLabel.visible = false;
            }
        }

        private function flash(message:String, point:Vector2, tint:Color):void {
            popupLabel.text = message;
            popupLabel.position = new Vector2(point.x - 70, point.y - 49);
            popupLabel.modulate = tint;
            popupLabel.visible = true;
            fade = POPUP_DURATION;
        }

        private function onStartPressed():void {
            startRequested.emit();
        }

        private function onDashPressed():void {
            dashRequested.emit();
        }

        private function digits(n:int):String {
            return n < 10 ? "0" + n : String(n);
        }
    }
}
