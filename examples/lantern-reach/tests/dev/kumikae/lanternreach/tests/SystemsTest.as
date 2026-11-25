package dev.kumikae.lanternreach.tests {
    import app.anicanon.kumikae.testing.Assert;
    import dev.kumikae.lanternreach.Scenario;
    import dev.kumikae.lanternreach.Dawn;
    import dev.kumikae.lanternreach.sim.*;

    public class SystemsTest {
        private var game:Simulation;
        private function clockTime():Number { return 0; }
        [Before]
        public function open():void { game = new Scenario("station", clockTime).simulation; }
        [After]
        public function close():void { game.stats.close(game.events); }

        [Test]
        public function fixedClockAndCommands():void {
            var clock:FixedStep = new FixedStep();
            Assert.assertTrue(clock.advance(0.5 / 60) == 0);
            Assert.assertTrue(clock.advance(0.5 / 60) == 1);
            game.commands.enqueue(new TickCommand(1, "move", 1, 0));
            game.step();
            Assert.assertTrue(game.keeper.x == 32);
            game.step();
            Assert.assertTrue(game.keeper.x > 32);
        }

        [Test]
        public function seawardSpawnAndWaves():void {
            var wave:Wave = new Wave();
            wave.tick = 1;
            wave.count = 4;
            wave.depth = 2;
            wave.name = 1;
            game.spawn.waves.push(wave);
            game.step();
            Assert.assertTrue(game.pool.count == 0);
            game.step();
            Assert.assertTrue(game.pool.count == 4 && game.stats.waves == 1);
            for each (var actor:Creature in game.pool.slots) Assert.assertTrue(actor.y < 2.1);
        }

        [Test]
        public function movingAndFlocking():void {
            var first:Creature = game.spawn.place(Creature.SWARMER, 20.2, 12, 0);
            var second:Creature = game.spawn.place(Creature.SWARMER, 20.3, 12, 0);
            game.step();
            game.step();
            Assert.assertTrue(first.x < second.x && first.y > 12 && first.vx != second.vx);
        }

        [Test]
        public function neighboursAndReuse():void {
            var near:Creature = game.spawn.place(0, 5.5, 4, 0);
            var far:Creature = game.spawn.place(0, 12, 4, 0);
            var spatial:SpatialIndex = new SpatialIndex(game.grid, game.pool);
            spatial.rebuild();
            var found:Vector.<Creature> = new Vector.<Creature>();
            spatial.nearby(5, 4, 1, found);
            Assert.assertTrue(found.length == 1 && found[0] == near);
            game.pool.release(far);
            Assert.assertTrue(game.spawn.place(1, 7, 8, 0) == far);
        }

        [Test]
        public function collisionThenDamage():void {
            var enemy:Creature = game.spawn.place(Creature.BRUTE, 8.45, 8, 0);
            game.shots.fire(8, 8, 1, 0, 7, 8, 1);
            game.step();
            Assert.assertTrue(enemy.hp == game.rules.enemies[Creature.BRUTE].health - 7);
            Assert.assertFalse(game.shots.shots[0].active);
        }

        [Test]
        public function burningSpreads():void {
            var first:Creature = game.spawn.place(Creature.BRUTE, 4, 3, 0);
            var other:Creature = game.spawn.place(Creature.BRUTE, 4.1, 3, 0);
            first.burn = 80;
            game.step();
            Assert.assertTrue(first.burn == 79 && other.burn > 0 && other.burn < first.burn);
        }

        [Test]
        public function shardsOfferDistinctChoices():void {
            game.pickups.drop(game.keeper.x, game.keeper.y, 20);
            game.step();
            Assert.assertTrue(game.keeper.level == 2 && game.keeper.offers.length == 3);
            Assert.assertTrue(game.keeper.offers[0] != game.keeper.offers[1] && game.keeper.offers[1] != game.keeper.offers[2]);
            game.commands.enqueue(new TickCommand(1, "choose", 0, 0));
            game.step();
            Assert.assertTrue(game.keeper.offers.length == 0);
        }

        [Test]
        public function occlusionHidesLurkers():void {
            game.town.build(10 * 64 + 10, Structure.LAMP);
            game.town.build(10 * 64 + 11, Structure.WALL);
            var shadow:Creature = game.spawn.place(Creature.LURKER, 12, 10, 0);
            game.light.update();
            var hp:int = shadow.hp;
            shadow.hurt(7);
            Assert.assertTrue(!shadow.lit && shadow.hp == hp);
            game.town.remove(10 * 64 + 11, true);
            game.light.update();
            shadow.hurt(7);
            Assert.assertTrue(shadow.lit && shadow.hp == hp - 7);
        }

        [Test]
        public function rigPaysAndSaleRefundsHalf():void {
            var cell:int = 10 * 64 + 10;
            game.town.build(cell, Structure.RIG);
            game.tick = game.rules.structures[Structure.RIG].period - 1;
            var coins:int = game.keeper.salvage;
            game.step();
            Assert.assertTrue(game.keeper.salvage == coins + 3);
            game.town.remove(cell, true);
            Assert.assertTrue(game.keeper.salvage == coins + 10);
        }

        [Test]
        public function eventsAndRemoval():void {
            var stats:RunStats = new RunStats(game.events);
            game.events.post(WorldEvent.BUILT, 1, 0);
            Assert.assertTrue(stats.built == 0);
            game.events.dispatch();
            stats.close(game.events);
            game.events.post(WorldEvent.BUILT, 2, 0);
            game.events.dispatch();
            Assert.assertTrue(stats.built == 1 && game.stats.built == 2);
        }

        private function onBuilt(item:WorldEvent):void {
            if (item.kind == WorldEvent.BUILT) game.events.post(WorldEvent.DESTROYED, item.cell, 0);
        }

        [Test]
        public function listenerCanPostFollowup():void {
            var listener:Subscription = game.events.listen(onBuilt);
            game.events.post(WorldEvent.BUILT, 3, 0);
            game.events.dispatch();
            game.events.dispatch();
            Assert.assertTrue(game.stats.destroyed == 1);
            game.events.remove(listener);
        }

        [Test]
        public function bossTelegraphsBeforeFlood():void {
            var boss:Creature = game.spawn.place(Creature.UNDERTOW, 25, 2, 0);
            boss.hp = 1000;
            boss.cooldown = 0;
            game.step();
            Assert.assertTrue(boss.warning == 1 && boss.flood == 0);
            for (var i:int = 0; i < 45; i++) game.step();
            Assert.assertTrue(boss.flood == 1 && game.grid.revision > 0);
        }

        [Test]
        public function splitAtMostThreeGenerations():void {
            var parent:Creature = game.spawn.place(Creature.SPLITTER, 12, 4, 2);
            parent.hp = 0;
            game.step();
            Assert.assertTrue(game.pool.count == 2);
            for each (var child:Creature in game.pool.slots) {
                if (child.active) { Assert.assertTrue(child.generation == 3); child.hp = 0; }
            }
            game.step();
            Assert.assertTrue(game.pool.count == 0 && game.stats.deaths == 3);
        }

        [Test]
        public function shoreSalvageSurvivesRestore():void {
            var tides:Tides = new Tides(game);
            tides.morning();
            var before:String = game.checksum();
            game.wrack[0] = -1;
            Assert.assertNotEquals(before, game.checksum());
            var saved:String = Dawn.write(game, 1);
            game.wrack.length = 0;
            Dawn.read(game, saved);
            Assert.assertTrue(game.wrack.length == 3 && game.wrack[0] == -1);
        }

        [Test]
        public function lastBeaconEndsWatch():void {
            var tides:Tides = new Tides(game);
            tides.morning();
            tides.dusk();
            for each (var beacon:int in game.grid.beacons) {
                var brute:Creature = game.spawn.place(Creature.BRUTE, beacon % 64 + 0.5, int(beacon / 64) + 0.5, 0);
                brute.cooldown = 0;
            }
            for (var i:int = 0; i < game.combat.beaconHealth.length; i++) game.combat.beaconHealth[i] = 1;
            tides.advance();
            Assert.assertTrue(tides.phase == "loss" && game.stats.lost == 3);
        }
    }
}
