package dev.kumikae.lanternreach.tests {
    import dev.kumikae.lanternreach.sim.*;
    import app.anicanon.kumikae.testing.Assert;

    public class CoreTest {
        [Test]
        public function randomSequence():void {
            var random:RandomStream = new RandomStream(1);
            Assert.assertEquals(270369, random.next());
            Assert.assertEquals(67634689, random.next());
        }

        [Test]
        public function fieldAvoidsWall():void {
            var grid:TownGrid = new TownGrid(5, 3);
            grid.beacons.push(9);
            grid.walls[7] = true;
            var field:FlowField = new FlowField(grid);
            field.rebuild();
            Assert.assertEquals(6, field.cost[5]);
            Assert.assertEquals(0, field.cost[9]);
        }
    }
}
