using System;
using Godot;

public partial class NBody : Node2D
{
    private const int Steps = 2000;
    private const double SolarMass = 4 * Math.PI * Math.PI;
    private const double Year = 365.24;

    private sealed class Body
    {
        public double X, Y, Z, Vx, Vy, Vz, Mass;

        public Body(double x, double y, double z, double vx, double vy, double vz, double mass)
        {
            X = x;
            Y = y;
            Z = z;
            Vx = vx;
            Vy = vy;
            Vz = vz;
            Mass = mass;
        }
    }

    private Body[] _bodies;

    public override void _Ready()
    {
        _bodies = new[]
        {
            new Body(0, 0, 0, 0, 0, 0, SolarMass),
            new Body(4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01,
                1.66007664274403694e-03 * Year, 7.69901118419740425e-03 * Year, -6.90460016972063023e-05 * Year,
                9.54791938424326609e-04 * SolarMass),
            new Body(8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01,
                -2.76742510726862411e-03 * Year, 4.99852801234917238e-03 * Year, 2.30417297573763929e-05 * Year,
                2.85885980666130812e-04 * SolarMass),
            new Body(1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01,
                2.96460137564761618e-03 * Year, 2.37847173959480950e-03 * Year, -2.96589568540237556e-05 * Year,
                4.36624404335156298e-05 * SolarMass),
            new Body(1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01,
                2.68067772490389322e-03 * Year, 1.62824170038242295e-03 * Year, -9.51592254519715870e-05 * Year,
                5.15138902046611451e-05 * SolarMass),
        };
        double px = 0, py = 0, pz = 0;
        foreach (Body b in _bodies)
        {
            px += b.Vx * b.Mass;
            py += b.Vy * b.Mass;
            pz += b.Vz * b.Mass;
        }
        _bodies[0].Vx = -px / SolarMass;
        _bodies[0].Vy = -py / SolarMass;
        _bodies[0].Vz = -pz / SolarMass;
    }

    public override void _Process(double delta)
    {
        for (int step = 0; step < Steps; step++)
        {
            Advance(0.01);
        }
    }

    private void Advance(double dt)
    {
        Body[] bodies = _bodies;
        int n = bodies.Length;
        for (int i = 0; i < n; i++)
        {
            Body bi = bodies[i];
            for (int j = i + 1; j < n; j++)
            {
                Body bj = bodies[j];
                double dx = bi.X - bj.X;
                double dy = bi.Y - bj.Y;
                double dz = bi.Z - bj.Z;
                double d2 = dx * dx + dy * dy + dz * dz;
                double mag = dt / (d2 * Math.Sqrt(d2));
                bi.Vx -= dx * bj.Mass * mag;
                bi.Vy -= dy * bj.Mass * mag;
                bi.Vz -= dz * bj.Mass * mag;
                bj.Vx += dx * bi.Mass * mag;
                bj.Vy += dy * bi.Mass * mag;
                bj.Vz += dz * bi.Mass * mag;
            }
        }
        for (int i = 0; i < n; i++)
        {
            Body b = bodies[i];
            b.X += dt * b.Vx;
            b.Y += dt * b.Vy;
            b.Z += dt * b.Vz;
        }
    }

    public double Checksum()
    {
        double e = 0.0;
        Body[] bodies = _bodies;
        int n = bodies.Length;
        for (int i = 0; i < n; i++)
        {
            Body bi = bodies[i];
            e += 0.5 * bi.Mass * (bi.Vx * bi.Vx + bi.Vy * bi.Vy + bi.Vz * bi.Vz);
            for (int j = i + 1; j < n; j++)
            {
                Body bj = bodies[j];
                double dx = bi.X - bj.X;
                double dy = bi.Y - bj.Y;
                double dz = bi.Z - bj.Z;
                e -= (bi.Mass * bj.Mass) / Math.Sqrt(dx * dx + dy * dy + dz * dz);
            }
        }
        return e;
    }
}
