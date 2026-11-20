package {
    import godot.*;

    public class Rooftop extends Node2D {
        [Export] public var cruiseSpeed:Number = 295;
        private var run:Round = new Round();
        private var hud:Hud;
        private var rider:CharacterBody2D;
        private var silver:Area2D;
        private var gold:Area2D;
        private var scanner:Node2D;
        private var best:int = 0;
        private var elapsed:Number = 0;
        private var silverStop:int = 0;
        private var goldStop:int = 0;
        private var heading:Vector2 = new Vector2(1, 0);
        private var dashHeading:Vector2 = new Vector2(1, 0);
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
            rider = getNode("Player") as CharacterBody2D;
            silver = getNode("Silver") as Area2D;
            gold = getNode("Gold") as Area2D;
            scanner = getNode("Scanner") as Node2D;
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
            elapsed = 0;
            silverStop = 0;
            goldStop = 0;
            silver.position = silverRoute[0];
            gold.position = goldRoute[0];
            rider.position = dock;
            rider.velocity = Vector2.ZERO;
            rider.modulate = Color.WHITE;
            heading = new Vector2(1, 0);
            moveScanner();
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
            dashHeading = heading;
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
                if (run.phase == Round.RUNNING && run.seconds <= 5 && int(Math.ceil(run.seconds)) < lastSecond) {
                    cue("Tick");
                }
            }
            if (run.phase == Round.FINISHED) finish();
            hud.update(run, best);
            hud.animate(delta);
        }

        private function drive(delta:Number):void {
            var direction:Vector2 = Input.getVector("move_left", "move_right", "move_up", "move_down");
            if (Input.isMouseButtonPressed(1)) {
                var pointer:Vector2 = getLocalMousePosition();
                if (pointer.y > 160 && pointer.y < 460) {
                    direction = (pointer - rider.position).limitLength(12) / 12;
                }
            }
            if (direction.length() > 0.1) heading = direction.normalized();
            if (Input.isActionJustPressed("ui_accept")) startDash();
            rider.velocity = run.boost > 0 ? dashHeading * 780 : direction * cruiseSpeed;
            rider.moveAndSlide();
            rider.position = new Vector2(clampf(rider.position.x, 91, 869), clampf(rider.position.y, 193, 448));
            elapsed += delta;
            moveScanner();
            if (absf(rider.position.x - scanner.position.x) < 16 &&
                rider.position.y > scanner.position.y - 19 && rider.position.y < scanner.position.y + Scanner.BEAM + 11) {
                if (run.hit()) {
                    hud.flash("−3 SEC", rider.position, new Color("#ffa18e"));
                    rider.position = dock;
                    rider.velocity = Vector2.ZERO;
                    cue("Hit");
                }
            }
            rider.modulate = run.grace > 0 && int(run.grace * 12) % 2 == 0 ? new Color("#ffffff70") : Color.WHITE;
        }

        private function moveScanner():void {
            // The phase is continuous when the late-round sweep accelerates.
            var fast:Number = Math.max(0, elapsed - 20);
            scanner.position = new Vector2(480 + 331 * sin(elapsed * 1.50 + fast * 0.38), 206);
        }

        private function takeSilver(body:Node2D):void {
            if (body != rider || run.phase != Round.RUNNING) return;
            run.collect(1);
            hud.flash("+1", silver.position, new Color("#abffea"));
            silverStop = (silverStop + 1) % silverRoute.length;
            silver.position = silverRoute[silverStop];
            cue("Silver");
        }

        private function takeGold(body:Node2D):void {
            if (body != rider || run.phase != Round.RUNNING) return;
            run.collect(3);
            hud.flash("+3 GOLD", gold.position, new Color("#ffe49a"));
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
