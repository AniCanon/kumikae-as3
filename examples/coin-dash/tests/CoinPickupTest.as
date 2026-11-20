package {
    import app.anicanon.kumikae.testing.Assert;
    import app.anicanon.kumikae.testing.TestScene;
    import godot.*;

    public class CoinPickupTest {
        [Test]
        public function pickupUpdatesHud():void {
            Coin.collected = 0;
            var scene:Node = TestScene.instantiate("res://main.tscn");
            var coin:Area2D = scene.getNode("Coin") as Area2D;
            var player:CharacterBody2D = scene.getNode("Player") as CharacterBody2D;
            var hud:Label = scene.getNode("Hud") as Label;
            player.position = coin.position;
            TestScene.until(function():Boolean {
                return hud.text == "Coins: 1";
            }, 5000, function():void {
                Assert.assertEquals(1, Coin.collected);
            });
        }
    }
}
