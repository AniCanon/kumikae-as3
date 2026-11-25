package dev.kumikae.lanternreach {
    import dev.kumikae.lanternreach.sim.*;
    import godot.Node2D;
    import godot.MultiMesh;
    import godot.MultiMeshInstance2D;
    import godot.QuadMesh;
    import godot.Shader;
    import godot.ShaderMaterial;
    import godot.ResourceLoader;
    import godot.Texture2D;
    import godot.Transform2D;
    import godot.Vector2;
    import godot.Color;
    import godot.Rect2;
    import godot.RenderingServer;

    public class Crowd extends Node2D {
        private var batches:Vector.<MultiMesh> = new Vector.<MultiMesh>();
        private var counts:Vector.<int> = new Vector.<int>();
        private var cursors:Vector.<int> = new Vector.<int>();

        private var ring:Texture2D;
        private var ringFrame:Rect2;
        private var origins:Vector.<Vector2> = new Vector.<Vector2>();
        private var started:Vector.<int> = new Vector.<int>();
        private var world:Simulation;
        private var struck:Vector.<int> = new Vector.<int>();
        private var tick:int = -10;
        private var listener:Subscription;

        private function receive(item:WorldEvent):void {
            if (item.kind == WorldEvent.TIDE_CALL) {
                var caller:Creature = world.pool.slots[item.value];
                if (!caller.active || caller.kind != Creature.TIDECALLER) return;
                origins.push(new Vector2(caller.x * 15, caller.y * 13));
                started.push(world.tick);
            }
            if (item.kind == WorldEvent.DAMAGE) struck[item.cell] = tick + 5;
        }

        public function close(bus:EventBus):void { bus.remove(listener); }

        public function setup(game:Simulation):void {
            world = game;
            ring = ResourceLoader.load("res://assets/art/effects.png") as Texture2D;
            var cell:int = int(ring.getWidth() / 3);
            ringFrame = new Rect2(0, cell, cell, cell);
            for (var j:int = 0; j < game.grid.walls.length; j++) struck.push(-1);
            listener = game.events.listen(receive);
            var rules:Rules = game.rules;
            var pigment:Shader = ResourceLoader.load("res://assets/crowd.gdshader") as Shader;
            for (var i:int = 0; i < rules.enemies.length; i++) {
                var node:MultiMeshInstance2D = new MultiMeshInstance2D();
                var shape:QuadMesh = new QuadMesh();
                var extent:int = rules.enemies[i].drawSize;
                shape.size = new Vector2(extent, extent);
                var batch:MultiMesh = new MultiMesh();
                batch.useCustomData = true;
                batch.mesh = shape;
                node.multimesh = batch;
                node.texture = ResourceLoader.load("res://assets/art/" + rules.enemies[i].key + ".png") as Texture2D;
                var paint:ShaderMaterial = new ShaderMaterial();
                paint.shader = pigment;
                paint.setShaderParameter("layout", i == Creature.UNDERTOW ? new Vector2(4, 2) : new Vector2(3, 2));
                node.material = paint;
                addChild(node);
                batches.push(batch);
                counts.push(0);
                cursors.push(0);
            }
        }

        private function paintCalls():void {
            RenderingServer.canvasItemClear(getCanvasItem());
            for (var i:int = started.length - 1; i >= 0; i--) {
                var phase:Number = (tick - started[i]) / 90.0;
                if (phase >= 1 || phase < 0) {
                    started.splice(i, 1);
                    origins.splice(i, 1);
                    continue;
                }
                var radius:Number = 8 + 23 * phase;
                var dest:Rect2 = new Rect2(origins[i].x - radius, origins[i].y - radius, 2 * radius, 2 * radius);
                RenderingServer.canvasItemAddTextureRectRegion(getCanvasItem(), dest, ring.getRID(), ringFrame, new Color(1, 1, 1, 0.8 * (1 - phase)));
            }
        }

        public function present(game:Simulation):void {
            tick = game.tick;
            if (started.length > 0) paintCalls();
            for (var n:int = 0; n < batches.length; n++) {
                counts[n] = 0;
                cursors[n] = 0;
            }
            for each (var actor:Creature in game.pool.slots) {
                if (actor.active && (actor.kind != Creature.LURKER || actor.lit)) counts[actor.kind]++;
            }
            for (var k:int = 0; k < batches.length; k++) {
                var mesh:MultiMesh = batches[k];
                if (mesh.instanceCount < counts[k]) mesh.instanceCount = counts[k];
                mesh.visibleInstanceCount = counts[k];
            }
            for each (var enemy:Creature in game.pool.slots) {
                if (!enemy.active || (enemy.kind == Creature.LURKER && !enemy.lit)) continue;
                var point:Vector2 = new Vector2(enemy.x * 15, enemy.y * 13);
                var angle:Number = Math.atan2(enemy.vx, -enemy.vy);
                var ratio:Number = 1.0 / (1 + 0.22 * enemy.generation);
                var transform:Transform2D = new Transform2D(angle, new Vector2(ratio, ratio), 0, point);
                var id:int = cursors[enemy.kind]++;
                var picture:int = enemy.kind == Creature.UNDERTOW ? 0 : int((game.tick + enemy.id) / 7) % 5;
                batches[enemy.kind].setInstanceTransform2D(id, transform);
                batches[enemy.kind].setInstanceCustomData(id, new Color(picture / 16.0, struck[game.grid.cell(enemy.x, enemy.y)] >= tick ? 1 : enemy.warning > 0 ? 0.45 : 0, 0, 1));
            }
        }
    }
}
