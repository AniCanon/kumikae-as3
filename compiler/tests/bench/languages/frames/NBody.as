package frames {
	import godot.Node2D;

	public class NBody extends Node2D {
		private static const STEPS:int = 2000;
		private static const SOLAR_MASS:Number = 4 * Math.PI * Math.PI;
		private static const YEAR:Number = 365.24;

		private var bodies:Vector.<Body> = new Vector.<Body>();

		override public function _ready():void {
			bodies.push(new Body(0, 0, 0, 0, 0, 0, SOLAR_MASS));
			bodies.push(new Body(4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01,
					1.66007664274403694e-03 * YEAR, 7.69901118419740425e-03 * YEAR, -6.90460016972063023e-05 * YEAR,
					9.54791938424326609e-04 * SOLAR_MASS));
			bodies.push(new Body(8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01,
					-2.76742510726862411e-03 * YEAR, 4.99852801234917238e-03 * YEAR, 2.30417297573763929e-05 * YEAR,
					2.85885980666130812e-04 * SOLAR_MASS));
			bodies.push(new Body(1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01,
					2.96460137564761618e-03 * YEAR, 2.37847173959480950e-03 * YEAR, -2.96589568540237556e-05 * YEAR,
					4.36624404335156298e-05 * SOLAR_MASS));
			bodies.push(new Body(1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01,
					2.68067772490389322e-03 * YEAR, 1.62824170038242295e-03 * YEAR, -9.51592254519715870e-05 * YEAR,
					5.15138902046611451e-05 * SOLAR_MASS));
			var px:Number = 0, py:Number = 0, pz:Number = 0;
			for (var i:int = 0; i < bodies.length; i++) {
				var b:Body = bodies[i];
				px += b.vx * b.mass;
				py += b.vy * b.mass;
				pz += b.vz * b.mass;
			}
			var sun:Body = bodies[0];
			sun.vx = -px / SOLAR_MASS;
			sun.vy = -py / SOLAR_MASS;
			sun.vz = -pz / SOLAR_MASS;
		}

		override public function _process(delta:Number):void {
			for (var step:int = 0; step < STEPS; step++) {
				advance(0.01);
			}
		}

		private function advance(dt:Number):void {
			var n:int = bodies.length;
			for (var i:int = 0; i < n; i++) {
				var bi:Body = bodies[i];
				for (var j:int = i + 1; j < n; j++) {
					var bj:Body = bodies[j];
					var dx:Number = bi.x - bj.x;
					var dy:Number = bi.y - bj.y;
					var dz:Number = bi.z - bj.z;
					var d2:Number = dx * dx + dy * dy + dz * dz;
					var mag:Number = dt / (d2 * Math.sqrt(d2));
					bi.vx -= dx * bj.mass * mag;
					bi.vy -= dy * bj.mass * mag;
					bi.vz -= dz * bj.mass * mag;
					bj.vx += dx * bi.mass * mag;
					bj.vy += dy * bi.mass * mag;
					bj.vz += dz * bi.mass * mag;
				}
			}
			for (i = 0; i < n; i++) {
				var b:Body = bodies[i];
				b.x += dt * b.vx;
				b.y += dt * b.vy;
				b.z += dt * b.vz;
			}
		}

		public function checksum():Number {
			var e:Number = 0;
			var n:int = bodies.length;
			for (var i:int = 0; i < n; i++) {
				var bi:Body = bodies[i];
				e += 0.5 * bi.mass * (bi.vx * bi.vx + bi.vy * bi.vy + bi.vz * bi.vz);
				for (var j:int = i + 1; j < n; j++) {
					var bj:Body = bodies[j];
					var dx:Number = bi.x - bj.x;
					var dy:Number = bi.y - bj.y;
					var dz:Number = bi.z - bj.z;
					e -= (bi.mass * bj.mass) / Math.sqrt(dx * dx + dy * dy + dz * dz);
				}
			}
			return e;
		}
	}
}

class Body {
	public var x:Number, y:Number, z:Number, vx:Number, vy:Number, vz:Number, mass:Number;

	public function Body(x:Number, y:Number, z:Number, vx:Number, vy:Number, vz:Number, mass:Number) {
		this.x = x;
		this.y = y;
		this.z = z;
		this.vx = vx;
		this.vy = vy;
		this.vz = vz;
		this.mass = mass;
	}
}
