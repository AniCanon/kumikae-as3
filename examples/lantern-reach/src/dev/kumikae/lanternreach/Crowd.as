package dev.kumikae.lanternreach {
    import dev.kumikae.lanternreach.sim.*;
    import godot.MultiMesh;
    import godot.MultiMeshInstance2D;
    import godot.QuadMesh;
    import godot.Transform2D;
    import godot.Vector2;
    import godot.Vector3;

    public class Crowd extends MultiMeshInstance2D {
        private var instances:MultiMesh = new MultiMesh();

        override public function _ready():void {
            var shape:QuadMesh = new QuadMesh();
            shape.size = new Vector2(6, 8);
            instances.mesh = shape;
            multimesh = instances;
        }

        public function present(creatures:CreaturePool):void {
            if (instances.instanceCount < creatures.count) instances.instanceCount = creatures.count;
            instances.visibleInstanceCount = creatures.count;
            var drawn:int = 0;
            for (var i:int = 0; i < creatures.slots.length; i++) {
                var actor:Creature = creatures.slots[i];
                if (!actor.active) continue;
                var point:Vector2 = new Vector2(actor.x * 14, actor.y * 13);
                instances.setInstanceTransform2D(drawn++, new Transform2D(0, point));
            }
        }
    }
}
