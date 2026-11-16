using Godot;

public partial class PerNodeProcess : Node2D
{
    private const int Count = 20000;

    private readonly Node2D[] _children = new Node2D[Count];

    public override void _Ready()
    {
        for (int i = 0; i < Count; i++)
        {
            var child = new PerNodeChild();
            AddChild(child);
            _children[i] = child;
        }
    }

    public double Checksum()
    {
        double sum = 0.0;
        foreach (Node2D child in _children)
        {
            sum += child.Rotation + child.Position.X;
        }
        return sum;
    }
}
