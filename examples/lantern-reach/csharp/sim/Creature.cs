using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class Creature : IDamageable {
    public const int SWARMER = 0;
    public const int BRUTE = 1;
    public const int WISP = 2;
    public const int SPLITTER = 3;
    public const int LURKER = 4;
    public const int TIDECALLER = 5;
    public const int UNDERTOW = 6;
    public int kind;
    public int generation;
    public int burn;
    public int slow;
    public int buff;
    public int cooldown;
    public int warning;
    public int flood;
    public int phase;
    public bool lit;
    public double speed;
    public int id;

    public void hurt(int amount) {
        if (kind != LURKER || lit) hp = Math.Max(0, hp - amount);
    }
    public bool active;
    public double x;
    public double y;
    public double vx;
    public double vy;
    public double steerX;
    public double steerY;
    public int hp;
    public int nextInCell;

    public void reset(int slot, double px, double py) {
        id = slot;
        x = px;
        y = py;
        vx = 0;
        vy = 0;
        steerX = 0;
        steerY = 0;
        hp = 10;
        kind = SWARMER;
        generation = 0;
        burn = 0;
        slow = 0;
        buff = 0;
        warning = 0;
        phase = 0;
        flood = 0;
        cooldown = slot % 60;
        speed = 0.05;
        lit = false;
        nextInCell = -1;
        active = true;
    }
}
