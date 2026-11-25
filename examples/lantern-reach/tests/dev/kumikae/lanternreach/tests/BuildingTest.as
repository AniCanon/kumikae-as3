package dev.kumikae.lanternreach.tests {
    import app.anicanon.kumikae.testing.Assert;
    import dev.kumikae.lanternreach.sim.*;

    public class BuildingTest {
        [Test]
        public function lastApproachStaysOpen():void {
            var grid:TownGrid = new TownGrid(3, 3);
            grid.beacons.push(7);
            grid.walls[3] = true;
            grid.walls[5] = true;
            var routes:BuildRoutes = new BuildRoutes(grid);
            Assert.assertFalse(routes.permits(4));
            Assert.assertFalse(grid.walls[4]);
            Assert.assertTrue(routes.permits(0));
        }
    }
}
