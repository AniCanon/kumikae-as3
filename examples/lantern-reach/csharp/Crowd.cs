using Godot;
using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public partial class Crowd : Node2D {
    private List<MultiMesh> batches = new List<MultiMesh>();
    private List<int> counts = new List<int>();
    private List<int> cursors = new List<int>();

    private readonly List<int> struck = new();
    private int tick = -10;
    private Subscription listener;

    private void receive(WorldEvent item) {
        if (item.kind == WorldEvent.DAMAGE) struck[item.cell] = tick + 5;
    }

    public void close(EventBus bus) { bus.remove(listener); }

    public void setup(Simulation game) {
        for (int i = 0; i < game.grid.walls.Count; i++) struck.Add(-1);
        listener = game.events.listen(receive);
        Rules rules = game.rules;
        Shader pigment = ResourceLoader.Load("res://assets/crowd.gdshader") as Shader;
        for (int i = 0; i < rules.enemies.Count; i++) {
            MultiMeshInstance2D node = new MultiMeshInstance2D();
            QuadMesh shape = new QuadMesh();
            int extent = rules.enemies[i].drawSize;
            shape.Size = new Vector2(extent, extent);
            MultiMesh batch = new MultiMesh();
            batch.UseCustomData = true;
            batch.Mesh = shape;
            node.Multimesh = batch;
            node.Texture = ResourceLoader.Load("res://assets/art/" + rules.enemies[i].key + ".png") as Texture2D;
            ShaderMaterial paint = new ShaderMaterial();
            paint.Shader = pigment;
            paint.SetShaderParameter("layout", i == Creature.UNDERTOW ? new Vector2(4, 2) : new Vector2(3, 2));
            node.Material = paint;
            AddChild(node);
            batches.Add(batch);
            counts.Add(0);
            cursors.Add(0);
        }
    }

    public void present(Simulation game) {
        tick = game.tick;
        for (int n = 0; n < batches.Count; n++) {
            counts[n] = 0;
            cursors[n] = 0;
        }
        foreach (Creature actor in game.pool.slots) {
            if (actor.active && (actor.kind != Creature.LURKER || actor.lit)) counts[actor.kind]++;
        }
        for (int k = 0; k < batches.Count; k++) {
            MultiMesh mesh = batches[k];
            if (mesh.InstanceCount < counts[k]) mesh.InstanceCount = counts[k];
            mesh.VisibleInstanceCount = counts[k];
        }
        foreach (Creature enemy in game.pool.slots) {
            if (!enemy.active || (enemy.kind == Creature.LURKER && !enemy.lit)) continue;
            Vector2 point = new Vector2((float)(enemy.x * 15), (float)(enemy.y * 13));
            double angle = Math.Atan2(enemy.vx, -enemy.vy);
            double ratio = 1.0 / (1 + 0.22 * enemy.generation);
            Transform2D transform = new Transform2D((float)angle, new Vector2((float)ratio, (float)ratio), 0, point);
            int id = cursors[enemy.kind]++;
            int picture = enemy.kind == Creature.UNDERTOW ? 0 : (int)((game.tick + enemy.id) / 7) % 5;
            batches[enemy.kind].SetInstanceTransform2D(id, transform);
            batches[enemy.kind].SetInstanceCustomData(id, new Color(picture / 16f, struck[game.grid.cell(enemy.x, enemy.y)] >= tick ? 1 : enemy.warning > 0 ? 0.45f : 0, 0, 1));
        }
    }
}
