using Godot;

public partial class PerNodeChild : Node2D
{
    public override void _Process(double delta)
    {
        Rotation += (float)delta;
        Vector2 p = Position;
        p.X += 10 * (float)delta;
        Position = p;
    }
}
