package dev.kumikae.lanternreach {
    import dev.kumikae.lanternreach.sim.*;
    import godot.FileAccess;

    public final class Definitions {
        private static function read(name:String):Array {
            return JSON.parse(FileAccess.getFileAsString("res://data/" + name + ".json")) as Array;
        }

        public static function load():Rules {
            var rules:Rules = new Rules();
            for each (var enemiesRow:Object in read("enemies")) {
                var enemiesDef:EnemyDef = new EnemyDef();
                enemiesDef.key = enemiesRow.key;
                enemiesDef.health = enemiesRow.health;
                enemiesDef.speed = enemiesRow.speed;
                enemiesDef.damage = enemiesRow.damage;
                enemiesDef.xp = enemiesRow.xp;
                enemiesDef.period = enemiesRow.period;
                rules.enemies.push(enemiesDef);
            }
            for each (var structuresRow:Object in read("structures")) {
                var structuresDef:StructureDef = new StructureDef();
                structuresDef.key = structuresRow.key;
                structuresDef.cost = structuresRow.cost;
                structuresDef.health = structuresRow.health;
                structuresDef.radius = structuresRow.radius;
                structuresDef.power = structuresRow.power;
                structuresDef.period = structuresRow.period;
                structuresDef.branchA = structuresRow.branchA;
                structuresDef.branchB = structuresRow.branchB;
                rules.structures.push(structuresDef);
            }
            for each (var weaponsRow:Object in read("weapons")) {
                var weaponsDef:WeaponDef = new WeaponDef();
                weaponsDef.key = weaponsRow.key;
                weaponsDef.damage = weaponsRow.damage;
                weaponsDef.range = weaponsRow.range;
                weaponsDef.period = weaponsRow.period;
                weaponsDef.pierce = weaponsRow.pierce;
                rules.weapons.push(weaponsDef);
            }
            for each (var upgradesRow:Object in read("upgrades")) {
                var upgradesDef:UpgradeDef = new UpgradeDef();
                upgradesDef.key = upgradesRow.key;
                upgradesDef.stat = upgradesRow.stat;
                upgradesDef.amount = upgradesRow.amount;
                upgradesDef.weight = upgradesRow.weight;
                rules.upgrades.push(upgradesDef);
            }
            for each (var page:Object in read("knowledge")) {
                var advice:Advice = new Advice();
                advice.key = page.key;
                advice.subject = page.subject;
                advice.value = page.value;
                rules.knowledge.push(advice);
            }
            for each (var tide:Object in read("nights")) {
                var plan:NightPlan = new NightPlan();
                plan.duration = tide.duration;
                for each (var tile:int in tide.shore) plan.shore.push(tile);
                for each (var group:Array in tide.waves) {
                    var wave:Wave = new Wave();
                    wave.tick = group[0];
                    wave.kind = group[1];
                    wave.count = group[2];
                    wave.depth = group[3];
                    wave.name = group[4];
                    plan.waves.push(wave);
                }
                rules.nights.push(plan);
            }
            return rules;
        }
    }
}
