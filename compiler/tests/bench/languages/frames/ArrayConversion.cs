using Godot;

public partial class ArrayConversion : Node2D
{
    private const int Rounds = 20;
    private const int Size = 1000;
    private static readonly StringName DataName = "data";

    private Line2D _line;

    public override void _Ready()
    {
        _line = new Line2D();
        AddChild(_line);
    }

    public override void _Process(double delta)
    {
        for (int k = 0; k < Rounds; k++)
        {
            var points = new Vector2[Size];
            for (int i = 0; i < Size; i++)
            {
                points[i] = new Vector2(i, k);
            }
            _line.Points = points;
            var data = new Godot.Collections.Array();
            for (int i = 0; i < Size; i++)
            {
                data.Add(i + k);
            }
            SetMeta(DataName, data);
        }
    }

    public double Checksum()
    {
        var data = (Godot.Collections.Array)GetMeta(DataName);
        return _line.GetPointCount() + data.Count;
    }
}
