using Godot;
using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public partial class MarshView : Node2D {
    private Node2D ground = new Node2D();
    private Node2D figures = new Node2D();
    private Crowd crowd = new Crowd();
    private ColorRect haze = new ColorRect();
    private ShaderMaterial fog = new ShaderMaterial();
    private ImageTexture chart;
    private Plate tiles;
    private Plate towers;
    private Plate beacons;
    private Plate debris;
    private Plate chains;
    private Plate walk;
    private Plate swing;
    private Plate dash;
    private byte[] pixels;
    private int revision = -1;
    public bool shake = true;
    public bool accessible;

    public void setup(Simulation game) {
        tiles = new Plate("tiles", 3, 8);
        towers = new Plate("structures", 3, 6);
        beacons = new Plate("beacons", 3, 1);
        debris = new Plate("effects", 3, 3);
        chains = new Plate("undertow", 4, 2);
        walk = new Plate("wren-walk", 2, 2);
        dash = new Plate("wren-dash", 2, 2);
        swing = new Plate("wren-swing", 2, 2);
        AddChild(ground);
        AddChild(crowd);
        crowd.setup(game);
        AddChild(figures);
        AddChild(haze);
        haze.MouseFilter = Control.MouseFilterEnum.Ignore;
        haze.Size = new Vector2(960, 520);
        fog.Shader = ResourceLoader.Load("res://assets/murk.gdshader") as Shader;
        haze.Material = fog;
        pixels = new byte[game.grid.walls.Count];
        chart = ImageTexture.CreateFromImage(Image.CreateFromData(game.grid.width, game.grid.height, false, Image.Format.L8, pixels));
        fog.SetShaderParameter("light_chart", chart);
    }

    private void drawGround(TownGrid grid) {
        Rid canvas = ground.GetCanvasItem();
        RenderingServer.CanvasItemClear(canvas);
        for (int y = 0; y < grid.height; y++) {
            for (int x = 0; x < grid.width; x++) {
                int band = y < 2 ? 0 : y < 4 ? 1 : y < 7 ? 2 : y < 10 ? 4 : 6;
                if (grid.marsh[y * grid.width + x]) band = 3;
                if (y >= 5 && (x % 16 < 2 || y == 19)) band = 5;
                if (grid.beacons.IndexOf(y * grid.width + x) >= 0) band = 7;
                int variant = (x * 7 + y * 5) % 2;
                if (y == 2 || y == 4 || y == 7 || y == 10) variant = 2;
                tiles.paint(canvas, band * 3 + variant, x * 15 + 7.5, y * 13 + 6.5, 15, 13);
            }
        }
    }

    public void close(EventBus bus) { crowd.close(bus); }

    public void present(Simulation game) {
        if (game.grid.revision != revision) {
            drawGround(game.grid);
            revision = game.grid.revision;
        }
        crowd.present(game);
        Rid canvas = figures.GetCanvasItem();
        RenderingServer.CanvasItemClear(canvas);
        foreach (Structure tower in game.town.placed) {
            towers.paint(canvas, tower.kind * 3 + tower.tier - 1, (tower.cell % game.grid.width + 0.5) * 15,
                ((int)(tower.cell / game.grid.width) + 0.5) * 13, 24, 24);
        }
        foreach (int beacon in game.grid.beacons) {
            beacons.paint(canvas, 2, (beacon % game.grid.width + 0.5) * 15, ((int)(beacon / game.grid.width) + 0.5) * 13, 40, 40);
        }
        foreach (Shot shot in game.shots.shots) {
            if (shot.active) debris.paint(canvas, 1, shot.x * 15, shot.y * 13, 12, 12);
        }
        foreach (Shard shard in game.pickups.shards) {
            if (shard.value > 0) debris.paint(canvas, 0, shard.x * 15, shard.y * 13, 8, 8);
        }
        foreach (Creature actor in game.pool.slots) {
            if (actor.active && actor.kind == Creature.UNDERTOW) {
                for (int j = 1; j <= 6; j++) {
                    double sway = Math.Sin(game.tick * 0.03 + j * 0.45) * (actor.warning > 0 ? 18 : 6);
                    chains.paint(canvas, j, actor.x * 15 + sway, actor.y * 13 + 15 + j * 8, 20, 20);
                }
            }
        }
        Plate hero = game.keeper.dash > 0 ? dash : game.keeper.lastShot > 10 ? swing : walk;
        Vector2 origin = new Vector2((float)(game.keeper.x * 15), (float)(game.keeper.y * 13));
        RenderingServer.CanvasItemAddSetTransform(canvas, new Transform2D((float)Math.Atan2(game.keeper.aimY, game.keeper.aimX), origin));
        hero.paint(canvas, (int)(game.tick / 5) % 4, 0, 0, 34, 34);
        for (int i = 0; i < game.light.level.Count; i++) pixels[i] = (byte)game.light.level[i];
        chart.Update(Image.CreateFromData(game.grid.width, game.grid.height, false, Image.Format.L8, pixels));
        fog.SetShaderParameter("tide", game.tick / 60.0);
        fog.SetShaderParameter("accessible", accessible);
    }
}
