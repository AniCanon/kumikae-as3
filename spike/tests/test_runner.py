"""The benchmark runner's command-line protocol, and the GDScript benchmarks' checksums."""
import math
import os
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import godot_bin  # noqa: E402
import run_bench  # noqa: E402


def nbody_energy(frames: int) -> float:
    """Plain-Python reference for the nbody benchmark: 2,000 steps of dt 0.01 per frame."""
    pi, solar_mass, year = 3.141592653589793, 4 * 3.141592653589793 ** 2, 365.24
    bodies = [
        [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, solar_mass],
        [4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01,
         1.66007664274403694e-03 * year, 7.69901118419740425e-03 * year, -6.90460016972063023e-05 * year,
         9.54791938424326609e-04 * solar_mass],
        [8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01,
         -2.76742510726862411e-03 * year, 4.99852801234917238e-03 * year, 2.30417297573763929e-05 * year,
         2.85885980666130812e-04 * solar_mass],
        [1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01,
         2.96460137564761618e-03 * year, 2.37847173959480950e-03 * year, -2.96589568540237556e-05 * year,
         4.36624404335156298e-05 * solar_mass],
        [1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01,
         2.68067772490389322e-03 * year, 1.62824170038242295e-03 * year, -9.51592254519715870e-05 * year,
         5.15138902046611451e-05 * solar_mass],
    ]
    px = py = pz = 0.0
    for b in bodies:
        px += b[3] * b[6]
        py += b[4] * b[6]
        pz += b[5] * b[6]
    bodies[0][3], bodies[0][4], bodies[0][5] = -px / solar_mass, -py / solar_mass, -pz / solar_mass
    dt = 0.01
    for _ in range(frames * 2000):
        for i in range(5):
            bi = bodies[i]
            for j in range(i + 1, 5):
                bj = bodies[j]
                dx, dy, dz = bi[0] - bj[0], bi[1] - bj[1], bi[2] - bj[2]
                d2 = dx * dx + dy * dy + dz * dz
                mag = dt / (d2 * math.sqrt(d2))
                bi[3] -= dx * bj[6] * mag
                bi[4] -= dy * bj[6] * mag
                bi[5] -= dz * bj[6] * mag
                bj[3] += dx * bi[6] * mag
                bj[4] += dy * bi[6] * mag
                bj[5] += dz * bi[6] * mag
        for b in bodies:
            b[0] += dt * b[3]
            b[1] += dt * b[4]
            b[2] += dt * b[5]
    e = 0.0
    for i in range(5):
        bi = bodies[i]
        e += 0.5 * bi[6] * (bi[3] * bi[3] + bi[4] * bi[4] + bi[5] * bi[5])
        for j in range(i + 1, 5):
            bj = bodies[j]
            dx, dy, dz = bi[0] - bj[0], bi[1] - bj[1], bi[2] - bj[2]
            e -= (bi[6] * bj[6]) / math.sqrt(dx * dx + dy * dy + dz * dz)
    return e


class RunnerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        godot_bin.ensure_imported()

    def test_verify_prints_checksum(self):
        self.assertEqual(run_bench.run("sieve", "gd", "verify")["checksum"], 17984 * 10)

    def test_measure_shape(self):
        row = run_bench.run("sieve", "gd", "measure")
        self.assertEqual(set(row), {"bench", "lang", "frames", "median_us", "p99_us", "max_us", "checksum"})
        self.assertEqual(row["frames"], 600)

    def test_warmup_excluded(self):
        os.environ["KAS3_SLOW_FIRST_FRAMES"] = "300"
        try:
            row = run_bench.run("empty", "gd", "measure")
        finally:
            del os.environ["KAS3_SLOW_FIRST_FRAMES"]
        self.assertLess(row["max_us"], 10000)

    def test_fixed_step(self):
        row = run_bench.run("empty", "gd", "verify")
        self.assertLess(abs(row["delta"] - 1 / 60), 1e-9)
        self.assertEqual(row["physics_ticks"], 10)

    def test_unknown_bench_exits_2(self):
        result = subprocess.run(run_bench.command("nope", "gd", "verify"), capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("unknown bench: nope", result.stderr)

    def test_unknown_lang_exits_2(self):
        result = subprocess.run(run_bench.command("sieve", "zz", "verify"), capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("unknown lang: zz", result.stderr)

    def test_nbody_energy(self):
        got = run_bench.run("nbody", "gd", "verify")["checksum"]
        want = nbody_energy(10)
        self.assertLess(abs(got - want), 1e-9 * abs(want))

    def test_every_gd_bench_runs(self):
        expected = {"move_nodes": None, "per_node_process": None, "ray_queries": None,
                    "signal_traffic": 10 * 2 * sum(range(20000)), "array_conversion": 2000}
        for bench, want in expected.items():
            with self.subTest(bench=bench):
                got = run_bench.run(bench, "gd", "verify")["checksum"]
                self.assertTrue(math.isfinite(got) and got != 0, got)
                if want is not None:
                    self.assertEqual(got, want)


if __name__ == "__main__":
    unittest.main()
