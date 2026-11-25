using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class TownGrid {
    public int floods;
    public int width;
    public int height;
    public List<bool> walls = new List<bool>();
    public List<bool> marsh = new List<bool>();
    public List<int> beacons = new List<int>();
    public int revision = 0;

    public TownGrid(int columns, int rows) {
        width = columns;
        height = rows;
        for (int i = 0; i < width * height; i++) {
            walls.Add(false);
            marsh.Add(false);
        }
    }

    public int cell(double x, double y) {
        return (int)(Math.Max(0, Math.Min(height - 1, y))) * width +
            (int)(Math.Max(0, Math.Min(width - 1, x)));
    }

    public int neighbour(int cellId, int direction) {
        if (direction == 0) return cellId >= width ? cellId - width : -1;
        if (direction == 1) return cellId % width > 0 ? cellId - 1 : -1;
        if (direction == 2) return cellId % width < width - 1 ? cellId + 1 : -1;
        return cellId + width < walls.Count ? cellId + width : -1;
    }
}
