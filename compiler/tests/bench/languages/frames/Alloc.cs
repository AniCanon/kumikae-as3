using Godot;

/// Holdout: allocation every frame.
public partial class Alloc : Node2D
{
    private const int Count = 20000;

    private sealed class Particle
    {
        public double X, Y, Vx, Vy;

        public Particle(double x, double y, double vx, double vy)
        {
            X = x;
            Y = y;
            Vx = vx;
            Vy = vy;
        }
    }

    private readonly Particle[] _slots = new Particle[Count];
    private int _frame;
    private double _last;

    public override void _Process(double delta)
    {
        _frame++;
        Particle[] slots = _slots;
        for (int i = 0; i < slots.Length; i++)
        {
            slots[i] = new Particle(i, _frame, i * 0.5, _frame * 0.25);
        }
        double sum = 0.0;
        for (int i = 0; i < slots.Length; i++)
        {
            Particle p = slots[i];
            sum += p.X + p.Vy;
        }
        _last = sum;
    }

    public double Checksum() => _last;
}
