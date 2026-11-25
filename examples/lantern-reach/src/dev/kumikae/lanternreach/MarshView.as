package dev.kumikae.lanternreach {
    import dev.kumikae.lanternreach.sim.*;
    import godot.Node2D;
    import godot.ColorRect;
    import godot.ShaderMaterial;
    import godot.Shader;
    import godot.ImageTexture;
    import godot.Image;
    import godot.PackedByteArray;
    import godot.RenderingServer;
    import godot.ResourceLoader;
    import godot.RID;
    import godot.Vector2;
    import godot.Transform2D;

    public class MarshView extends Node2D {
        private var ground:Node2D = new Node2D();
        private var figures:Node2D = new Node2D();
        private var crowd:Crowd = new Crowd();
        private var haze:ColorRect = new ColorRect();
        private var fog:ShaderMaterial = new ShaderMaterial();
        private var chart:ImageTexture;
        private var tiles:Plate;
        private var towers:Plate;
        private var beacons:Plate;
        private var debris:Plate;
        private var chains:Plate;
        private var walk:Plate;
        private var swing:Plate;
        private var dash:Plate;
        private var pixels:PackedByteArray;
        private var revision:int = -1;
        public var shake:Boolean = true;
        public var accessible:Boolean;

        public function setup(game:Simulation):void {
            for each (var cell:int in game.grid.beacons) original.push(cell);
            tiles = new Plate("tiles", 3, 8);
            towers = new Plate("structures", 3, 6);
            beacons = new Plate("beacons", 3, 1);
            debris = new Plate("effects", 3, 3);
            chains = new Plate("undertow", 4, 2);
            walk = new Plate("wren-walk", 2, 2);
            dash = new Plate("wren-dash", 2, 2);
            swing = new Plate("wren-swing", 2, 2);
            addChild(ground);
            addChild(crowd);
            crowd.setup(game);
            addChild(figures);
            addChild(haze);
            haze.mouseFilter = 2;
            haze.size = new Vector2(960, 520);
            fog.shader = ResourceLoader.load("res://assets/murk.gdshader") as Shader;
            haze.material = fog;
            pixels = new PackedByteArray();
            pixels.resize(game.grid.walls.length);
            chart = ImageTexture.createFromImage(Image.createFromData(game.grid.width, game.grid.height, false, Image.FORMAT_L8, pixels));
            fog.setShaderParameter("light_chart", chart);
        }

        private function drawGround(grid:TownGrid):void {
            var canvas:RID = ground.getCanvasItem();
            RenderingServer.canvasItemClear(canvas);
            for (var y:int = 0; y < grid.height; y++) {
                for (var x:int = 0; x < grid.width; x++) {
                    var band:int = y < 2 ? 0 : y < 4 ? 1 : y < 7 ? 2 : y < 10 ? 4 : 6;
                    if (grid.marsh[y * grid.width + x]) band = 3;
                    if (y >= 5 && (x % 16 < 2 || y == 19)) band = 5;
                    if (grid.beacons.indexOf(y * grid.width + x) >= 0) band = 7;
                    var variant:int = (x * 7 + y * 5) % 2;
                    if (y == 2 || y == 4 || y == 7 || y == 10) variant = 2;
                    tiles.paint(canvas, band * 3 + variant, x * 15 + 7.5, y * 13 + 6.5, 15, 13);
                }
            }
        }

        public function close(bus:EventBus):void { crowd.close(bus); }

        public function present(game:Simulation):void {
            if (game.grid.revision != revision) {
                drawGround(game.grid);
                revision = game.grid.revision;
            }
            crowd.present(game);
            var canvas:RID = figures.getCanvasItem();
            RenderingServer.canvasItemClear(canvas);
            for each (var tower:Structure in game.town.placed) {
                towers.paint(canvas, tower.kind * 3 + tower.tier - 1, (tower.cell % game.grid.width + 0.5) * 15,
                    (int(tower.cell / game.grid.width) + 0.5) * 13, 24, 24);
            }
            for each (var beacon:int in original) {
                beacons.paint(canvas, game.grid.beacons.indexOf(beacon) >= 0 || sunrise > 0 ? 2 : 0, (beacon % game.grid.width + 0.5) * 15, (int(beacon / game.grid.width) + 0.5) * 13, 40, 40);
            }
            for (var k:int = 0; k < game.wrack.length; k++) {
                var cell:int = game.wrack[k];
                if (cell < 0) continue;
                var x:Number = cell % game.grid.width + 0.5;
                var y:Number = int(cell / game.grid.width) + 0.5;
                if (marked[k] || Math.abs(game.keeper.x - x) + Math.abs(game.keeper.y - y) < 12)
                    debris.paint(canvas, 5, x * 15, y * 13, marked[k] ? 22 : 14, marked[k] ? 22 : 14);
            }
            for each (var shot:Shot in game.shots.shots) {
                if (shot.active) debris.paint(canvas, 1, shot.x * 15, shot.y * 13, 12, 12);
            }
            for each (var shard:Shard in game.pickups.shards) {
                if (shard.value > 0) debris.paint(canvas, 0, shard.x * 15, shard.y * 13, 8, 8);
            }
            for each (var actor:Creature in game.pool.slots) {
                if (actor.active && actor.kind == Creature.UNDERTOW) {
                    for (var j:int = 1; j <= 6; j++) {
                        var sway:Number = Math.sin(game.tick * 0.03 + j * 0.45) * (actor.warning > 0 ? 18 : 6);
                        chains.paint(canvas, j, actor.x * 15 + sway, actor.y * 13 + 15 + j * 8, 20, 20);
                    }
                }
            }
            var hero:Plate = game.keeper.dash > 0 ? dash : game.keeper.lastShot > 10 ? swing : walk;
            var origin:Vector2 = new Vector2(game.keeper.x * 15, game.keeper.y * 13);
            RenderingServer.canvasItemAddSetTransform(canvas, new Transform2D(Math.atan2(game.keeper.aimY, game.keeper.aimX), origin));
            hero.paint(canvas, int(game.tick / 5) % 4, 0, 0, 34, 34);
            for (var i:int = 0; i < game.light.level.length; i++) pixels.set(i, game.light.level[i]);
            chart.update(Image.createFromData(game.grid.width, game.grid.height, false, Image.FORMAT_L8, pixels));
            fog.setShaderParameter("tide", game.tick / 60.0);
            fog.setShaderParameter("sunrise", sunrise);
            fog.setShaderParameter("accessible", accessible);
        }
        public var marked:Vector.<Boolean> = new <Boolean>[false, false, false];
        private var original:Vector.<int> = new Vector.<int>();
        private var sunrise:Number;

        public function pullBack(frame:int):void {
            sunrise = Math.min(1, frame / 150.0);
            var zoom:Number = 1.12 - sunrise * 0.12;
            scale = new Vector2(zoom, zoom);
            position = new Vector2(480 * (1 - zoom), 260 * (1 - zoom));
        }

    }
}
