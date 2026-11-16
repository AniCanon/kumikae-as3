using Godot;

public partial class Sieve : Node2D
{
    private const int Limit = 200000;

    private readonly int[] _flags = new int[Limit];
    private int _total;

    public override void _Process(double delta)
    {
        int[] flags = _flags;
        for (int i = 0; i < flags.Length; i++)
        {
            flags[i] = 1;
        }
        int count = 0;
        for (int i = 2; i < flags.Length; i++)
        {
            if (flags[i] == 1)
            {
                count++;
                int j = i + i;
                while (j < flags.Length)
                {
                    flags[j] = 0;
                    j += i;
                }
            }
        }
        _total += count;
    }

    public double Checksum() => _total;
}
