using Godot;

public partial class SignalTraffic : Node2D
{
    private const int Emits = 20000;

    [Signal]
    public delegate void HitEventHandler(int amount, Vector2 pos);

    private double _total;

    public override void _Ready()
    {
        Hit += OnHit;
    }

    public override void _Process(double delta)
    {
        for (int i = 0; i < Emits; i++)
        {
            EmitSignal(SignalName.Hit, i, new Vector2(i, 1));
        }
    }

    private void OnHit(int amount, Vector2 pos)
    {
        _total += amount + pos.X;
    }

    public double Checksum() => _total;
}
