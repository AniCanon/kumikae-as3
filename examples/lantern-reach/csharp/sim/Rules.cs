using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class Rules {
    public List<EnemyDef> enemies = new List<EnemyDef>();
    public List<StructureDef> structures = new List<StructureDef>();
    public List<WeaponDef> weapons = new List<WeaponDef>();
    public List<UpgradeDef> upgrades = new List<UpgradeDef>();
}
