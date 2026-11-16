using Godot;

public partial class RayQueries : Node2D
{
    private const int Rays = 2000;
    private static readonly Variant PositionKey = "position";

    private PhysicsRayQueryParameters2D _params;
    private int _hits;
    private double _sumX;

    public override void _Ready()
    {
        for (int gy = 0; gy < 8; gy++)
        {
            for (int gx = 0; gx < 8; gx++)
            {
                var body = new StaticBody2D();
                var shape = new CollisionShape2D();
                var circle = new CircleShape2D();
                circle.Radius = 20;
                shape.Shape = circle;
                body.AddChild(shape);
                body.Position = new Vector2(gx * 100 + 50, gy * 100 + 50);
                AddChild(body);
            }
        }
        _params = new PhysicsRayQueryParameters2D();
    }

    public override void _PhysicsProcess(double delta)
    {
        PhysicsDirectSpaceState2D space = GetWorld2D().DirectSpaceState;
        PhysicsRayQueryParameters2D query = _params;
        for (int i = 0; i < Rays; i++)
        {
            query.From = new Vector2(-50, i % 800);
            query.To = new Vector2(850, (i * 7) % 800);
            Godot.Collections.Dictionary result = space.IntersectRay(query);
            if (result.Count > 0)
            {
                _hits++;
                Vector2 pos = (Vector2)result[PositionKey];
                _sumX += pos.X;
            }
        }
    }

    public double Checksum() => _hits + _sumX;
}
