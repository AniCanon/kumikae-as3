package dev.kumikae.coindash.gameplay {
    import dev.kumikae.coindash.config.RoundRules;
    import dev.kumikae.coindash.entities.Player;
    import dev.kumikae.coindash.entities.Scanner;
    import dev.kumikae.coindash.presentation.Hud;
    import dev.kumikae.coindash.storage.Ledger;
    import godot.*;

    public class Rooftop extends Node2D {
        private static const POINTER_EASE_RADIUS:Number = 12;
        private static const POINTER_TOP:Number = 160;
        private static const POINTER_BOTTOM:Number = 460;

        [Export] public var cruiseSpeed:Number = 295;
        private var run:Round = new Round();
        private var hud:Hud;
        private var rider:Player;
        private var silver:Area2D;
        private var gold:Area2D;
        private var scanner:Scanner;
        private var best:int = 0;
        private var deck:Rect2 = new Rect2(91, 193, 778, 255);
        private var silverStop:int = 0;
        private var goldStop:int = 0;
        private var dock:Vector2 = new Vector2(465, 407);
        private var silverRoute:Vector.<Vector2> = new <Vector2>[
            new Vector2(578, 398), new Vector2(284, 416), new Vector2(731, 386),
            new Vector2(411, 366), new Vector2(171, 382), new Vector2(652, 424)
        ];
        private var goldRoute:Vector.<Vector2> = new <Vector2>[
            new Vector2(713, 258), new Vector2(210, 244), new Vector2(643, 283),
            new Vector2(321, 265)
        ];

        override public function _ready():void {
            if (Engine.isEditorHint()) return;
            rider = getNode("Player") as Player;
            silver = getNode("Silver") as Area2D;
            gold = getNode("Gold") as Area2D;
            scanner = getNode("Scanner") as Scanner;
            hud = new Hud(getNode("HUD") as Control);
            silver.bodyEntered.add(takeSilver);
            gold.bodyEntered.add(takeGold);
            (getNode("HUD/Menu/Action") as Button).pressed.add(startRun);
            (getNode("HUD/Burst") as Button).pressed.add(startDash);
            best = Ledger.loadBest();
            resetDeck();
            hud.update(run, best);
        }

        private function resetDeck():void {
            silverStop = 0;
            goldStop = 0;
            silver.position = silverRoute[0];
            gold.position = goldRoute[0];
            rider.resetAt(dock);
            scanner.resetPatrol();
        }

        public function startRun():void {
            run.start();
            resetDeck();
            hud.playing();
            hud.update(run, best);
            cue("Start");
        }

        private function startDash():void {
            if (!run.dash()) return;
            cue("Dash");
        }

        override public function _physicsProcess(delta:Number):void {
            if (Engine.isEditorHint() || hud == null) return;
            if (Input.isActionJustPressed("restart") ||
                (run.phase != Round.RUNNING && Input.isActionJustPressed("ui_accept"))) {
                startRun();
                return;
            }
            if (run.phase != Round.RUNNING) return;
            var lastSecond:int = int(Math.ceil(run.seconds));
            run.tick(delta);
            if (run.phase == Round.RUNNING) {
                drive(delta);
                if (run.phase == Round.RUNNING && run.seconds <= RoundRules.COUNTDOWN_WARNING_SECONDS &&
                    int(Math.ceil(run.seconds)) < lastSecond) {
                    cue("Tick");
                }
            }
            if (run.phase == Round.FINISHED) finish();
            hud.update(run, best);
            hud.animate(delta);
        }

        private function drive(delta:Number):void {
            if (Input.isActionJustPressed("ui_accept")) startDash();
            rider.drive(steering(), run.boost > 0, cruiseSpeed, deck);
            scanner.sweep(delta);
            if (scanner.covers(rider.position) && run.hit()) {
                hud.showPenalty(rider.position);
                rider.position = dock;
                rider.velocity = Vector2.ZERO;
                cue("Hit");
            }
            rider.showGrace(run.grace);
        }

        private function steering():Vector2 {
            var direction:Vector2 = Input.getVector("move_left", "move_right", "move_up", "move_down");
            if (Input.isMouseButtonPressed(1)) {
                var pointer:Vector2 = getLocalMousePosition();
                if (pointer.y > POINTER_TOP && pointer.y < POINTER_BOTTOM) {
                    direction = (pointer - rider.position).limitLength(POINTER_EASE_RADIUS) / POINTER_EASE_RADIUS;
                }
            }
            return direction;
        }

        private function takeSilver(body:Node2D):void {
            if (body != rider || run.phase != Round.RUNNING) return;
            run.collectSilver();
            hud.showSilver(silver.position);
            silverStop = (silverStop + 1) % silverRoute.length;
            silver.position = silverRoute[silverStop];
            cue("Silver");
        }

        private function takeGold(body:Node2D):void {
            if (body != rider || run.phase != Round.RUNNING) return;
            run.collectGold();
            hud.showGold(gold.position);
            goldStop = (goldStop + 1) % goldRoute.length;
            gold.position = goldRoute[goldStop];
            cue("Gold");
        }

        private function finish():void {
            var newRecord:Boolean = run.score > best;
            if (newRecord) {
                best = run.score;
                Ledger.saveBest(best);
            }
            rider.velocity = Vector2.ZERO;
            rider.modulate = Color.WHITE;
            hud.finished(run, newRecord);
            cue("Finish");
        }

        private function cue(name:String):void {
            if (DisplayServer.getName() == "headless") return;
            (getNode("Audio/" + name) as AudioStreamPlayer).play();
        }
    }
}
