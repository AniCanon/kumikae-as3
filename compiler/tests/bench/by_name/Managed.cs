using Godot;
using System.Collections.Generic;

public partial class Managed : Node
{
    private static readonly StringName[] Fields = { "score", "bonus", "health", "speed", "level", "lives", "energy", "armor" };
    private static readonly StringName Bonus = "bonus";
    private static readonly StringName Score = "score";
    private static readonly StringName Current = "current";
    private static readonly StringName Caption = "caption";
    private static readonly StringName Heading = "heading";
    private static readonly StringName Peer = "peer";
    private readonly List<Node2D> targets = new();

    public void AddTarget(Node2D value) => targets.Add(value);

    public Variant EngineKeysRead(Node2D target, int n)
    {
        StringName[] props = { Node2D.PropertyName.Position, CanvasItem.PropertyName.Modulate, Node2D.PropertyName.Rotation };
        Variant value = default;
        for (int i = 0; i < n; ++i) value = target.Get(props[i % 3]);
        return value;
    }

    public double Measure(string kind, string op, Node2D target, int n)
    {
        if (kind == "configure" || kind == "configureTyped")
        {
            double total = 0;
            for (int i = 0; i < n; i++)
            {
                var timer = new Timer();
                timer.Set(Timer.PropertyName.WaitTime, 2.5);
                timer.Set(Timer.PropertyName.OneShot, true);
                timer.Set(Timer.PropertyName.Autostart, true);
                total += timer.Get(Timer.PropertyName.WaitTime).AsDouble();
                target.AddChild(timer);
                timer.Free();
            }
            return total;
        }
        if (kind == "timer" || kind == "timerTyped")
        {
            var timer = new Timer();
            double total = 0;
            for (int i = 0; i < n; i++) total += timer.Get(Timer.PropertyName.WaitTime).AsDouble();
            timer.Free();
            return total;
        }
        if ((kind == "proven" || kind == "typed") && op == "call")
        {
            double total = 0;
            for (int i = 0; i < n; i++)
            {
                var fresh = new Node2D();
                fresh.Call(Node2D.MethodName.Rotate, 0.0001);
                total += fresh.Rotation;
                fresh.Free();
            }
            return total;
        }
        if (kind == "proven" || kind == "typed" || kind == "detached")
        {
            var fresh = new Node2D();
            if (op == "read") fresh.Position = new Vector2(3, 4);
            double result = Measure("node", op, fresh, n);
            if (op == "write") result = fresh.Position.X + fresh.Position.Y;
            if (op == "call") result = fresh.Rotation;
            fresh.Free();
            return result;
        }
        if (kind == "methodKeys")
        {
            StringName[] keys = { CanvasItem.MethodName.IsVisibleInTree, Node.MethodName.IsProcessing };
            Variant method = default;
            for (int i = 0; i < n; ++i) method = target.Get(keys[i % 2]);
            return method.AsCallable().Call().AsBool() ? 1 : 0;
        }
        if (kind == "eightFields")
        {
            int total = 0;
            for (int i = 0; i < n; ++i) total += target.Get(Fields[i % 8]).AsInt32();
            return total;
        }
        if (kind == "computed")
        {
            StringName[] keys = { Score, Bonus };
            int total = 0;
            for (int i = 0; i < n; i++) total += target.Get(keys[i % 2]).AsInt32();
            return total;
        }
        if (kind == "customFields")
        {
            double total = 0;
            for (int i = 0; i < n; i++)
            {
                var caption = target.Get(Caption).AsString();
                var heading = target.Get(Heading).AsVector2();
                var peer = target.Get(Peer).AsGodotObject();
                total += caption.Length + heading.X + peer.Call(Current).AsInt32();
            }
            return total;
        }
        if (kind == "customString" || kind == "customVector" || (kind == "custom" && op == "call"))
        {
            double total = 0;
            if (kind == "customString")
                for (int i = 0; i < n; i++)
                    total += target.Get(Caption).AsString().Length + target.Call(Current).AsInt32();
            else if (kind == "customVector")
                for (int i = 0; i < n; i++)
                    total += target.Get(Heading).AsVector2().X + target.Call(Current).AsInt32();
            else
                for (int i = 0; i < n; i++)
                    total += target.Call(Current).AsInt32() + target.Call(Node.MethodName.GetChildCount).AsInt32();
            return total;
        }
        bool mixed = kind == "mixed";
        bool script = kind == "script" || kind == "foreign" || kind == "custom";
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
