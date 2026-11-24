using Godot;

namespace Kumikae.LanternReach;

public partial class Crowd : MultiMeshInstance2D
{
    private readonly MultiMesh instances = new();

    public override void _Ready()
    {
        instances.Mesh = new QuadMesh { Size = new Vector2(6, 8) };
        Multimesh = instances;
    }

    public void Present(CreaturePool creatures)
    {
        if (instances.InstanceCount < creatures.count) instances.InstanceCount = creatures.count;
        instances.VisibleInstanceCount = creatures.count;
        int drawn = 0;
        foreach (var actor in creatures.slots)
        {
            if (!actor.active) continue;
            var point = new Vector2((float)(actor.x * 14), (float)(actor.y * 13));
            instances.SetInstanceTransform2D(drawn++, new Transform2D(0, point));
        }
    }
}
