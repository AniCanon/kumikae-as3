using Godot;

/// Engine-overhead baseline: a script whose _Process does nothing.
public partial class Empty : Node2D
{
    public override void _Process(double delta)
    {
    }

    public double Checksum() => 0.0;
}
