using Godot;

/// Holdout: a unit-stride numeric loop.
public partial class Saxpy : Node2D
{
    private const int N = 100000;
    private const int Passes = 10;
    private const double A = 0.000001;

    private readonly double[] _x = new double[N];
    private readonly double[] _y = new double[N];
    private double _last;

    public override void _Ready()
    {
        for (int i = 0; i < N; i++)
        {
            _x[i] = i * 0.5;
            _y[i] = i;
        }
    }

    public override void _Process(double delta)
    {
        double[] x = _x;
        double[] y = _y;
        for (int p = 0; p < Passes; p++)
        {
            for (int i = 0; i < y.Length; i++)
            {
                y[i] = A * x[i] + y[i];
            }
        }
        double sum = 0.0;
        for (int i = 0; i < y.Length; i++)
        {
            sum += y[i];
        }
        _last = sum;
    }

    public double Checksum() => _last;
}
