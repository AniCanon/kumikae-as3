using System;
using Godot;

/// Holdout: objects and overridable method calls.
public partial class Shapes : Node2D
{
    private const int Count = 9000;
    private const int Passes = 10;

    private abstract class Shape
    {
        public abstract double Area();
        public abstract void Grow(double d);
    }

    private sealed class Circle : Shape
    {
        private double _r;
        public Circle(double r) { _r = r; }
        public override double Area() => Math.PI * _r * _r;
        public override void Grow(double d) { _r += d; }
    }

    private sealed class Rect : Shape
    {
        private double _w, _h;
        public Rect(double w, double h) { _w = w; _h = h; }
        public override double Area() => _w * _h;
        public override void Grow(double d) { _w += d; _h += d; }
    }

    private sealed class Tri : Shape
    {
        private double _b, _h;
        public Tri(double b, double h) { _b = b; _h = h; }
        public override double Area() => 0.5 * _b * _h;
        public override void Grow(double d) { _b += d; }
    }

    private readonly Shape[] _shapes = new Shape[Count];
    private double _total;

    public override void _Ready()
    {
        for (int i = 0; i < Count; i++)
        {
            double size = 1.0 + i * 0.001;
            _shapes[i] = (i % 3) switch
            {
                0 => new Circle(size),
                1 => new Rect(size, 2.0),
                _ => new Tri(3.0, size),
            };
        }
    }

    public override void _Process(double delta)
    {
        Shape[] shapes = _shapes;
        double total = _total;
        for (int p = 0; p < Passes; p++)
        {
            for (int i = 0; i < shapes.Length; i++)
            {
                Shape s = shapes[i];
                s.Grow(0.001);
                total += s.Area();
            }
        }
        _total = total;
    }

    public double Checksum() => _total;
}
