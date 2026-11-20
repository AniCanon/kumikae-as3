package dev.kumikae.coindash.tests {
    import dev.kumikae.coindash.entities.Scanner;
    import dev.kumikae.coindash.gameplay.Round;
    import app.anicanon.kumikae.testing.Assert;
    import app.anicanon.kumikae.testing.TestScene;
    import godot.*;

    public class CoinPickupTest {
        [Test]
        public function thirtySecondsAndNoOvertime():void {
            var run:Round = new Round();
            run.start();
            run.tick(29.5);
            run.collectGold();
            run.tick(0.5);
            run.collectSilver();
            Assert.assertEquals(3, run.score);
            Assert.assertEquals(1, run.gold);
            Assert.assertEquals(0, run.silver);
            Assert.assertEquals(0, run.seconds);
            Assert.assertEquals(Round.FINISHED, run.phase);
            run.start();
            Assert.assertEquals(0, run.score);
            Assert.assertEquals(30, run.seconds);
        }

        [Test]
        public function hitCostsTimeAndDashProtects():void {
            var run:Round = new Round();
            run.start();
            Assert.assertTrue(run.dash());
            Assert.assertFalse(run.hit());
            Assert.assertFalse(run.dash());
            run.tick(0.25);
            Assert.assertTrue(run.hit());
            Assert.assertFalse(run.hit());
            Assert.assertEquals(26.75, run.seconds);
        }

        [Test]
        public function scannerPatrolResetsToItsScenePosition():void {
            var scene:Node = TestScene.instantiate("res://main.tscn");
            var scanner:Scanner = scene.getNode("Scanner") as Scanner;
            var dock:Vector2 = scanner.position;
            scanner.sweep(1);
            Assert.assertTrue(scanner.position.x > dock.x);
            Assert.assertTrue(scanner.covers(scanner.position + new Vector2(0, 50)));
            Assert.assertFalse(scanner.covers(scanner.position + new Vector2(30, 50)));
            scanner.resetPatrol();
            Assert.assertEquals(dock.x, scanner.position.x);
            Assert.assertEquals(dock.y, scanner.position.y);
        }

        [Test]
        public function pickupUpdatesHud():void {
            var scene:Node = TestScene.instantiate("res://main.tscn");
            scene.call("startRun");
            var coin:Area2D = scene.getNode("Silver") as Area2D;
            var gold:Area2D = scene.getNode("Gold") as Area2D;
            var player:CharacterBody2D = scene.getNode("Player") as CharacterBody2D;
            var score:Label = scene.getNode("HUD/Score") as Label;
            player.position = coin.position;
            TestScene.until(function():Boolean {
                return score.text == "01";
            }, 3000, function():void {
                player.position = gold.position;
            }).until(function():Boolean {
                return score.text == "04";
            }, 3000, function():void {
                Assert.assertEquals("04", score.text);
            });
        }
    }
}
