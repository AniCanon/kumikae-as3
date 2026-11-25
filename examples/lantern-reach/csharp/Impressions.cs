using Godot;
using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public partial class Impressions : Node2D {
    private List<Sprite2D> bodies = new List<Sprite2D>();
    private List<Label> numbers = new List<Label>();
    private List<int> remaining = new List<int>();
    private List<Plate> deaths = new List<Plate>();
    private Rules rules;
    private Plate signs;
    private int cursor;
    private int width;
    private int impact;
    private Subscription listener;
    public bool shake = true;

    public void setup(Simulation game) {
        rules = game.rules;
        width = game.grid.width;
        signs = new Plate("effects", 3, 3);
        foreach (EnemyDef def in game.rules.enemies) {
            deaths.Add(new Plate(def.key, def.key == "undertow" ? 4 : 3, 2));
        }
        for (int i = 0; i < 64; i++) {
            Sprite2D body = new Sprite2D();
            Label number = new Label();
            body.RegionEnabled = true;
            body.Visible = false;
            number.Visible = false;
            number.AddThemeFontSizeOverride("font_size", 12);
            number.AddThemeConstantOverride("outline_size", 3);
            AddChild(body);
            body.AddChild(number);
            bodies.Add(body);
            numbers.Add(number);
            remaining.Add(0);
        }
        listener = game.events.listen(receive);
    }

    public void close(EventBus bus) { bus.remove(listener); }

    private void receive(WorldEvent item) {
        if (item.kind != WorldEvent.DEATH && item.kind != WorldEvent.DAMAGE &&
                item.kind != WorldEvent.FIRED && item.kind != WorldEvent.SWING && item.kind != WorldEvent.BEACON_LOST) return;
        Plate plate = item.kind == WorldEvent.DEATH ? deaths[item.value] : signs;
        int frameIndex = item.kind == WorldEvent.DEATH ? (item.value == Creature.UNDERTOW ? 0 : 5) :
            item.kind == WorldEvent.FIRED ? (item.value == Structure.BELL ? 3 : item.value == Structure.BRAZIER ? 2 : 1) :
            item.kind == WorldEvent.SWING ? 7 : 8;
        Sprite2D body = bodies[cursor];
        int w = (int)(plate.ink.GetWidth() / plate.columns);
        int h = (int)(plate.ink.GetHeight() / plate.rows);
        body.Texture = plate.ink;
        body.RegionRect = new Rect2(frameIndex % plate.columns * w, (int)(frameIndex / plate.columns) * h, w, h);
        float scale = item.kind == WorldEvent.DEATH ? rules.enemies[item.value].drawSize / (float)w : 0.5f;
        body.Scale = new Vector2(scale, scale);
        body.Position = new Vector2((item.cell % width + 0.5f) * 15, ((int)(item.cell / width) + 0.5f) * 13);
        body.Visible = true;
        Label number = numbers[cursor];
        number.Visible = item.kind == WorldEvent.DAMAGE;
        if (number.Visible) {
            number.Text = item.value.ToString();
            number.Scale = new Vector2(2, 2);
        }
        if (item.kind == WorldEvent.BEACON_LOST || item.kind == WorldEvent.DAMAGE) impact = 4;
        remaining[cursor] = 14;
        cursor = (cursor + 1) % bodies.Count;
    }

    public void present(Node2D view) {
        for (int i = 0; i < remaining.Count; i++) {
            if (remaining[i] <= 0) continue;
            remaining[i]--;
            Sprite2D body = bodies[i];
            body.Visible = remaining[i] > 0;
            body.Modulate = new Color(1, 1, 1, remaining[i] / 14f);
            if (numbers[i].Visible) body.Position = body.Position + new Vector2(0, -0.5f);
        }
        if (view.Scale.X == 1) view.Position = new Vector2(shake && impact > 0 ? impact % 2 * 2 - 1 : 0, 0);
        if (impact > 0) impact--;
    }
}
