package dev.kumikae.coindash.gameplay {
    import dev.kumikae.coindash.config.InputAction;
    import dev.kumikae.coindash.config.RoundRules;
    import dev.kumikae.coindash.entities.Coin;
    import dev.kumikae.coindash.entities.Player;
    import dev.kumikae.coindash.entities.Scanner;
    import dev.kumikae.coindash.presentation.Hud;
    import dev.kumikae.coindash.presentation.SoundBank;
    import dev.kumikae.coindash.storage.Ledger;
    import godot.Engine;
    import godot.Input;
    import godot.Node2D;
    import godot.Rect2;
    import godot.Vector2;

    /** Root rooftop coordinator: wired via typed [Export] scene references and compile-time [Signal]s. */
    public class Rooftop extends Node2D {
        private static const POINTER_EASE_RADIUS:Number = 12;
        private static const POINTER_TOP:Number = 160;
        private static const POINTER_BOTTOM:Number = 460;

        [Export] public var cruiseSpeed:Number = 295;
        [Export] public var rider:Player;
        [Export] public var silver:Coin;
        [Export] public var gold:Coin;
        [Export] public var scanner:Scanner;
        [Export] public var hud:Hud;
        [Export] public var sounds:SoundBank;

        private var run:Round = new Round();
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
            if (Engine.isEditorHint()) {
                return;
            }
            silver.collected.add(takeSilver);
            gold.collected.add(takeGold);
            hud.startRequested.add(startRun);
            hud.dashRequested.add(startDash);
            best = Ledger.loadBest();
            resetDeck();
            hud.update(run, best);
        }

        public function startRun():void {
            run.start();
            resetDeck();
            hud.playing();
            hud.update(run, best);
            sounds.playStart();
        }

        public function currentPhase():int {
            return run.phase;
        }

        private function resetDeck():void {
            silverStop = 0;
            goldStop = 0;
            silver.position = silverRoute[0];
            gold.position = goldRoute[0];
            rider.resetAt(dock);
            scanner.resetPatrol();
        }

        private function startDash():void {
            if (!run.dash()) {
                return;
            }
            sounds.playDash();
        }

        override public function _physicsProcess(delta:Number):void {
            if (Engine.isEditorHint() || hud == null) {
                return;
            }
            if (Input.isActionJustPressed(InputAction.RESTART) ||
                (run.phase != Round.RUNNING && Input.isActionJustPressed(InputAction.DASH))) {
                startRun();
                return;
            }
            if (run.phase != Round.RUNNING) {
                return;
            }
            var lastSecond:int = int(Math.ceil(run.seconds));
            run.tick(delta);
            if (run.phase == Round.RUNNING) {
                drive(delta);
                if (run.phase == Round.RUNNING && run.seconds <= RoundRules.COUNTDOWN_WARNING_SECONDS &&
                    int(Math.ceil(run.seconds)) < lastSecond) {
                    sounds.playTick();
                }
            }
            if (run.phase == Round.FINISHED) {
                finish();
            }
            hud.update(run, best);
            hud.animate(delta);
        }

        private function drive(delta:Number):void {
            if (Input.isActionJustPressed(InputAction.DASH)) {
                startDash();
            }
            rider.drive(steering(), run.boost > 0, cruiseSpeed, deck);
            scanner.sweep(delta);
            if (scanner.covers(rider.position) && run.hit()) {
                hud.showPenalty(rider.position);
                rider.position = dock;
                rider.velocity = Vector2.ZERO;
                sounds.playHit();
            }
            rider.showGrace(run.grace);
        }

        private function steering():Vector2 {
            var direction:Vector2 = Input.getVector(
                InputAction.MOVE_LEFT, InputAction.MOVE_RIGHT, InputAction.MOVE_UP, InputAction.MOVE_DOWN);
            if (Input.isMouseButtonPressed(1)) {
                var pointer:Vector2 = getLocalMousePosition();
                if (pointer.y > POINTER_TOP && pointer.y < POINTER_BOTTOM) {
                    direction = (pointer - rider.position).limitLength(POINTER_EASE_RADIUS) / POINTER_EASE_RADIUS;
                }
            }
            return direction;
        }

        private function takeSilver():void {
            if (run.phase != Round.RUNNING) {
                return;
            }
            run.collectSilver();
            hud.showSilver(silver.position);
            silverStop = (silverStop + 1) % silverRoute.length;
            silver.position = silverRoute[silverStop];
            sounds.playSilver();
        }

        private function takeGold():void {
            if (run.phase != Round.RUNNING) {
                return;
            }
            run.collectGold();
            hud.showGold(gold.position);
            goldStop = (goldStop + 1) % goldRoute.length;
            gold.position = goldRoute[goldStop];
            sounds.playGold();
        }

        private function finish():void {
            var newRecord:Boolean = run.score > best;
            if (newRecord) {
                best = run.score;
                Ledger.saveBest(best);
            }
            rider.velocity = Vector2.ZERO;
            rider.clearGrace();
            hud.finished(run, newRecord);
            sounds.playFinish();
        }
    }
}
