using Godot;

public partial class MoveNodes : Node2D
{
    private const int Count = 20000;

    private readonly Node2D[] _children = new Node2D[Count];
    private Vector2 _velocity = new Vector2(60, 30);

    public override void _Ready()
    {
        for (int i = 0; i < Count; i++)
        {
            var child = new Node2D();
            child.Position = new Vector2(i % 1000, i / 1000);
            AddChild(child);
            _children[i] = child;
        }
    }

    public override void _Process(double delta)
    {
        Node2D[] children = _children;
        float step = (float)delta;
        for (int i = 0; i < children.Length; i++)
        {
            Node2D child = children[i];
            Vector2 p = child.Position;
            p += _velocity * step;
            if (p.X > 1000)
            {
                p.X -= 1000;
            }
            child.Position = p;
        }
    }

    public double Checksum()
    {
        double sum = 0.0;
        foreach (Node2D child in _children)
        {
            sum += child.Position.X;
        }
        return sum;
    }
}
