using Godot;
using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class Plate {
    public Texture2D ink;
    public int columns;
    public int rows;
    private int width;
    private int height;

    public Plate(string name, int across, int down) {
        ink = ResourceLoader.Load("res://assets/art/" + name + ".png") as Texture2D;
        columns = across;
        rows = down;
        width = (int)(ink.GetWidth() / columns);
        height = (int)(ink.GetHeight() / rows);
    }

    public void paint(Rid canvas, int index, double x, double y, double w, double h) {
        Rect2 src = new Rect2(index % columns * width, (int)(index / columns) * height, width, height);
        Rect2 dest = new Rect2((float)(x - w * 0.5), (float)(y - h * 0.5), (float)w, (float)h);
        RenderingServer.CanvasItemAddTextureRectRegion(canvas, dest, ink.GetRid(), src);
    }
}
