using Godot;
using System.Collections.Generic;

public partial class Managed : Node
{
    private static readonly StringName Score = "score";
    private static readonly StringName Current = "current";
    private readonly List<Node2D> targets = new();

    public void AddTarget(Node2D value) => targets.Add(value);

    public double Measure(string kind, string op, Node2D target, int n)
    {
        bool mixed = kind == "mixed";
        bool script = kind == "script";
        double sum = 0;
        if (op == "read" && script)
            for (int i = 0; i < n; i++) sum += target.Get(Score).AsInt32();
        else if (op == "write" && script)
            for (int i = 0; i < n; i++) target.Set(Score, i);
        else if (op == "call" && script)
            for (int i = 0; i < n; i++) sum += target.Call(Current).AsInt32();
        else if (op == "read")
            for (int i = 0; i < n; i++)
                sum += (mixed ? targets[i & 7] : target).Get(Node2D.PropertyName.Position).AsVector2().X;
        else if (op == "write")
        {
            var point = new Vector2(8, 9);
            for (int i = 0; i < n; i++)
                (mixed ? targets[i & 7] : target).Set(Node2D.PropertyName.Position, point);
        }
        else
            for (int i = 0; i < n; i++)
                (mixed ? targets[i & 7] : target).Call(Node2D.MethodName.Rotate, 0.0001);
        return sum;
    }
}
