package {
    import app.anicanon.kumikae.testing.Assert;
    import app.anicanon.kumikae.testing.TestScene;
    import godot.*;

    public class CoinPickupTest {
        [Test]
        public function thirtySecondsAndNoOvertime():void {
            var run:Round = new Round();
            run.start();
            run.tick(29.5);
            run.collect(3);
            run.tick(0.5);
            run.collect(1);
            Assert.assertEquals(3, run.score);
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
