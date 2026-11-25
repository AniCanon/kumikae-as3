using Godot;
using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public partial class Orders : Node2D {
    public bool active;
    public bool day;
    public string tool = "lamp";
    private Simulation game;
    private Sprite2D cursor = new();
    private bool tapped;
    private Vector2 point = new Vector2();
    private int finger = -1;
    private Vector2 origin = new Vector2();
    private Vector2 drag = new Vector2();

    public void setup(Simulation sim) {
        game = sim;
        cursor.Texture = ResourceLoader.Load("res://assets/art/controls.png") as Texture2D;
        cursor.Hframes = 3;
        cursor.Vframes = 2;
        cursor.Scale = new Vector2(0.3f, 0.3f);
        cursor.Visible = false;
        AddChild(cursor);
    }

    public void send(string verb, double x, double y) {
        game.commands.enqueue(new TickCommand(game.tick, verb, x, y));
    }

    public override void _UnhandledInput(InputEvent input) {
        if (!active) return;
        InputEventScreenTouch touch = input as InputEventScreenTouch;
        if (touch != null) {
            if (touch.Pressed && (day || touch.Position.X < 480) && finger < 0) {
                finger = touch.Index;
                origin = touch.Position;
                drag = new Vector2();
            } else if (!touch.Pressed && touch.Index == finger) {
                if (day && drag.Length() < 6) { point = touch.Position; tapped = true; }
                finger = -1;
                drag = new Vector2();
            } else if (touch.Pressed) {
                aimAt(touch.Position);
                send("fire", 1, 0);
            }
        }
        InputEventScreenDrag motion = input as InputEventScreenDrag;
        if (motion != null && motion.Index == finger) drag = (motion.Position - origin).LimitLength(60);
        InputEventMouseButton mouse = input as InputEventMouseButton;
        if (!OS.HasFeature("mobile") && mouse != null && mouse.Pressed && mouse.ButtonIndex == MouseButton.Left && day) {
            point = mouse.Position;
            tapped = true;
        }
    }

    private void aimAt(Vector2 where) {
        double x = where.X / 15 - game.keeper.x;
        double y = where.Y / 13 - game.keeper.y;
        if (x != 0 || y != 0) send("aim", x, y);
    }

    public void poll() {
        cursor.Position = GetLocalMousePosition();
        cursor.Visible = !OS.HasFeature("mobile");
        cursor.Frame = day ? 5 : 4;
        Vector2 dir = Input.GetVector("move_left", "move_right", "move_up", "move_down");
        if (finger >= 0) dir = drag / 60;
        send("move", dir.X, dir.Y);
        Vector2 stick = Input.GetVector("aim_left", "aim_right", "aim_up", "aim_down");
        if (day) {
            if (Input.IsActionJustPressed("place")) {
                point = new Vector2((float)(game.keeper.x * 15), (float)(game.keeper.y * 13));
                tapped = true;
            }
            if (tapped) {
                send(tool, (int)(point.X / 15), (int)(point.Y / 13));
                tapped = false;
            }
        } else {
            if (stick.Length() > 0) send("aim", stick.X, stick.Y);
            else if (!OS.HasFeature("mobile")) aimAt(GetLocalMousePosition());
            if (!OS.HasFeature("mobile")) send("fire", Input.IsActionPressed("swing") || stick.Length() > 0 ? 1 : 0, 0);
            if (Input.IsActionJustPressed("dash")) send("dash", 0, 0);
        }
    }
}
