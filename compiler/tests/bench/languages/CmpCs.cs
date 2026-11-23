using Godot;

public partial class CmpCs : Node2D
{
    private static readonly StringName Accept = "ui_accept";
    private double total = 0;
    private int steps = 0;

    private static readonly StringName Score = "score";
    private static readonly StringName Current = "current";

    public int NamedLoop(Node2D target, bool scripted, int rounds)
    {
        StringName property = scripted ? Score : CanvasItem.PropertyName.Visible;
        StringName method = scripted ? Current : CanvasItem.MethodName.IsVisibleInTree;
        int sum = 0;
        for (int i = 0; i < rounds; i++)
        {
            target.Set(property, scripted ? Variant.From(i & 1) : Variant.From((i & 1) != 0));
            var read = target.Get(property);
            var called = target.Call(method);
            sum += scripted ? read.AsInt32() + called.AsInt32() : (read.AsBool() ? 1 : 0) + (called.AsBool() ? 1 : 0);
        }
        return sum;
    }

    public double Spin(int rounds)
    {
        var step = new Vector2(0.5f, 0.25f);
        int pressed = 0;
        for (int i = 0; i < rounds; i++)
        {
            Position = Position + step;
            Rotation += 0.001f;
            if (IsInsideTree()) pressed++;
            if (Input.IsActionPressed(Accept)) pressed--;
        }
        return Position.X + pressed;
    }

    public int Work(int n)
    {
        int checksum = 0;
        for (int i = 0; i < n; i++)
        {
            total += Scaled(i, 0.5);
            if ((i & 1023) == 0) checksum ^= Bump(i);
        }
        return checksum;
    }

    private double Scaled(int value, double by) => value * by + 1;

    private int Bump(int value)
    {
        steps++;
        return value % 97 + steps;
    }

    public double Vec(int n)
    {
        var v = Vector2.Zero;
        var d = new Vector2(0.5f, 0.25f);
        for (int i = 0; i < n; i++)
        {
            v = v + d * 0.5f;
            v = v.Normalized() * 3.0f;
        }
        return v.X;
    }

    public double Vec3(int n)
    {
        var v = Vector3.Zero;
        var d = new Vector3(0.5f, 0.25f, 0.125f);
        double sum = 0;
        for (int i = 0; i < n; i++)
        {
            v = v + d * 0.5f;
            v = v.Normalized() * 3.0f;
            sum += v.Dot(d);
        }
        return sum;
    }
}
