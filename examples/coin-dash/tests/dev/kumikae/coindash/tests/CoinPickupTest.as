package dev.kumikae.coindash.tests {
    import app.anicanon.kumikae.testing.Assert;
    import app.anicanon.kumikae.testing.TestScene;
    import dev.kumikae.coindash.entities.Scanner;
    import dev.kumikae.coindash.gameplay.Rooftop;
    import dev.kumikae.coindash.gameplay.Round;
    import godot.Vector2;

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
            var rooftop:Rooftop = TestScene.instantiate("res://main.tscn") as Rooftop;
            var scanner:Scanner = rooftop.scanner;
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
            var rooftop:Rooftop = TestScene.instantiate("res://main.tscn") as Rooftop;
            Assert.assertTrue(rooftop.hud.isMenuVisible());
            rooftop.hud.startRequested.emit();
            Assert.assertFalse(rooftop.hud.isMenuVisible());
            Assert.assertEquals(Round.RUNNING, rooftop.currentPhase());

            rooftop.rider.position = rooftop.silver.position;
            TestScene.until(function():Boolean {
                return rooftop.hud.scoreText() == "01";
            }, 3000, function():void {
                rooftop.rider.position = rooftop.gold.position;
            }).until(function():Boolean {
                return rooftop.hud.scoreText() == "04";
            }, 3000, function():void {
                Assert.assertEquals("04", rooftop.hud.scoreText());
            });
        }
    }
}
