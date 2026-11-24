"""Generated code inside Godot, for run_godot.py: scripts built into a program library and run by the
extension in the interpreter's place (KAS3_PROGRAM_LIBRARY).

These are held:

  probe      tests/godot_native's scripts, and the test project's Places.as, Emits.as, EmitsChild.as, Loops.as, Callables.as, Kinds.as and ByName.as with them, say, as
             native code, what they say interpreted, line for line. Built with another number for the runtime's layout
             in its table (runtime/layout.h), the library is refused and the interpreter runs them.
  workloads  each workload of tests/bench/languages' CmpAs.as, run small, stays on the fast paths.
  frames     each frame benchmark of tests/bench/languages/frames, run for a few frames, leaves to the
             library what frames/native_counts.txt says: nothing in a loop that should be free of it.

The second is by the host's counts (KAS3_NATIVE_COUNTS): how many times generated code left an operation
to the library (a thunk), how many of the places it asks Godot directly the host could not work out, how
many handles on Godot's values and objects it had the host copy, and how many times it asked the engine
for the object behind a handle. `native_counts.txt` beside each script holds the numbers, which do not grow
with a loop that is on its fast paths: a change that loses one makes a number thousands, and fails here and
not only in a timing. When a number is
meant to change, the failure says what was counted; write that into the file.
"""
import glob
import os
import shutil
import subprocess
import tempfile
import sys

# What generated code is built with (the design's flags), as strictly as our own, into a library that
# shows one symbol and needs none of ours.
FLAGS = ["-std=c++17", "-O2", "-fno-exceptions", "-fno-rtti", "-ffp-contract=off", "-fno-unroll-loops", "-Wall", "-Wextra", "-Werror",
         "-fvisibility=hidden", "-shared"]
# The editor's first scan of a project, which is given time (see run_godot.py).
FIRST_SCAN = ["--headless", "--editor", "--quit-after", "300"]
USED = "ActionScript: the scripts run as native code."
OTHER_LAYOUT = "it was built for another version of the runtime: build the program library again"
# How many rounds a workload is run for the counts: enough that a thunk in its loop shows.
ROUNDS = 1000
# And how many frames a frame benchmark is run for.
FRAMES = 3


def retained_on_macos(program, reloads=8):
    """Load a distinct Play image each time, then count all images still mapped after close."""
    import _ctypes
    import ctypes
    dyld = ctypes.CDLL(None)
    dyld._dyld_get_image_name.restype = ctypes.c_char_p

    def loaded():
        return {os.path.realpath(os.fsdecode(dyld._dyld_get_image_name(i)))
                for i in range(dyld._dyld_image_count())}

    with tempfile.TemporaryDirectory() as folder:
        paths = set()
        for i in range(reloads):
            path = os.path.realpath(shutil.copyfile(program, os.path.join(folder, f"play-{i}.dylib")))
            handle = _ctypes.dlopen(path, os.RTLD_NOW | os.RTLD_LOCAL)
            try:
                if path not in loaded():
                    raise RuntimeError(f"dyld did not report {path} after dlopen")
            finally:
                _ctypes.dlclose(handle)
            paths.add(path)
        return len(paths & loaded())


class Runner:
    def __init__(self, godot, extension, tests, kas3c, check, counted=False):
        self.godot = godot
        self.extension = extension  # A project that holds kas3.gdextension and the built library.
        self.tests = tests
        self.kas3c = kas3c
        self.check = check
        self.compiler = shutil.which("c++")
        self.counted = counted

    def able(self, what):
        if self.kas3c and os.path.exists(self.kas3c) and self.compiler:
            return True
        print("godot: %s was not run (it needs kas3c and a C++ compiler)" % what)
        return False

    def project(self, where, sources, files=None):
        """A project in `where` of a folder's files (all, or those named: a path names a file of elsewhere), with
        the extension; scanned once."""
        made = os.path.join(where, "project")
        if files is None:
            shutil.copytree(sources, made)
        else:
            os.makedirs(made)
            for name in files:
                shutil.copy(os.path.join(sources, name), made)
        shutil.copy(os.path.join(self.extension, "kas3.gdextension"), made)
        os.makedirs(os.path.join(made, "bin"))
        binaries = os.path.join(self.extension, "bin", "test_counts" if self.counted else "")
        for library in glob.glob(os.path.join(binaries, "libkas3_godot.*")):
            shutil.copy(library, os.path.join(made, "bin"))
        subprocess.run([self.godot, *FIRST_SCAN, "--path", made], capture_output=True, text=True, timeout=300)
        return made

    def library(self, label, project, scripts, where, other_layout=False):
        """The project's scripts as a program library, or None: what went wrong is a failed check. With
        other_layout, its table says it was built for another layout of the runtime than the one it was."""
        out = os.path.join(where, "native_other" if other_layout else "native")
        written = subprocess.run([self.kas3c, "cpp", "--godot", *scripts, "-o", out], capture_output=True, text=True, cwd=project, timeout=300)
        self.check("native: %s is written as C++" % label, written.returncode == 0, written.stderr[-600:])
        if written.returncode != 0:
            return None
        if other_layout:
            table = os.path.join(out, "program.cpp")
            text = open(table).read()
            self.check("native: the program's table starts with the runtime's layout", text.count("{ RUNTIME_LAYOUT, ") == 1, text[-400:])
            with open(table, "w") as file:
                file.write(text.replace("{ RUNTIME_LAYOUT, ", "{ RUNTIME_LAYOUT + 1, "))
        root = os.path.dirname(self.tests)
        program = os.path.join(where, "libkas3_program_other.dylib" if other_layout else "libkas3_program.dylib")
        includes = [flag for each in ("runtime", "vm", "ir", "src") for flag in ("-I", os.path.join(root, each))]
        built = subprocess.run([self.compiler, *FLAGS, *includes, *sorted(glob.glob(os.path.join(out, "*.cpp"))), "-o", program],
                               capture_output=True, text=True, timeout=600)
        self.check("native: the C++ of %s builds without a warning" % label, built.returncode == 0, built.stderr[-900:])
        return program if built.returncode == 0 else None

    def run(self, project, script, library, arguments=(), flags=(), extra_env=None):
        environment = dict(os.environ, KAS3_NATIVE_COUNTS="1", **(extra_env or {}))
        environment.pop("KAS3_PROGRAM_LIBRARY", None)
        if library:
            environment["KAS3_PROGRAM_LIBRARY"] = library
        command = [self.godot, "--headless", *flags, "--path", project, "-s", script, *(["--", *arguments] if arguments else [])]
        try:
            ran = subprocess.run(command, capture_output=True, text=True, timeout=300, env=environment)
        except subprocess.TimeoutExpired as late:
            return -1, "it ran for more than 300 seconds\n" + (late.stdout or b"").decode(errors="replace")[-2000:]
        return ran.returncode, ran.stdout + ran.stderr

    def counts(self, label, text, listed, name):
        """Holds what the host counted in a run to the file's line for it."""
        wanted = dict(line.split(": ", 1) for line in open(listed).read().splitlines() if line and not line.startswith("#"))
        found = [line for line in text.split("\n") if line.startswith("kas3: counts ")]
        detail = "\n".join(line for line in text.split("\n") if line.startswith("kas3:   "))
        self.check("native: %s says what it counted" % label, len(found) == 1, text[-600:])
        if len(found) == 1:
            counted = found[0][len("kas3: counts "):].strip()
            self.check("native: %s leaves to the library what %s says" % (label, os.path.basename(listed)), counted == wanted.get(name),
                       "\nlisted   %s\ncounted  %s\n%s" % (wanted.get(name), counted, detail))

    def method_allocations(self):
        with tempfile.TemporaryDirectory() as where:
            files = ["project.godot", os.path.join(self.tests, "bench/by_name/Matrix.as"),
                     os.path.join(self.tests, "method_allocations.gd")]
            project = self.project(where, self.extension, files)
            program = self.library("the computed method benchmark", project, ["Matrix.as"], where)
            if not program:
                return
            for label, library in (("interpreter", None), ("native", program)):
                status, text = self.run(project, "method_allocations.gd", library, extra_env={"KAS3_METHOD_ALLOCATIONS": "1"})
                allocations = [int(line.split()[-1]) for line in text.splitlines() if line.startswith("kas3: method allocations ")]
                self.check(f"{label}: warmed computed method reads allocate only the bound value",
                           status == 0 and (USED in text) == bool(library) and "METHOD true" in text
                           and len(allocations) >= 16 and max(allocations[2:16]) <= 1, str(allocations) + text[-600:])

                self.check(f"{label}: warmed script method reads allocate only the bound value",
                           "SCRIPT METHOD true" in text and len(allocations) == 48 and max(allocations[17:32]) <= 1,
                           str(allocations[16:]) + text[-600:])

                gets = [int(line.split()[-1]) for line in text.splitlines() if line.startswith("kas3: method named gets ")]
                self.check(f"{label}: only the scripted receiver delegates at a mixed computed site",
                           "MIXED true" in text and gets[32:] == [1, 0] * 8, str(gets[32:]))

    def by_name_reads(self, project, library):
        label = "native" if library else "interpreter"
        status, text = self.run(project, "by_name.gd", library, extra_env={"KAS3_TRACE_COMPUTED_NAME": "isProcessingInput"})
        self.check(f"{label}: counted method reads run in the requested mode",
                   status == 0 and (USED in text) == bool(library) and "SCRIPT ERROR" not in text, text[-1200:])
        lookups = text.count("kas3: resolved computed name isProcessingInput")
        self.check(f"{label}: a computed method resolves once on each engine class", lookups == 2, str(lookups))
        return status, text

    def probe(self):
        """The probe by the interpreter and as native code: what each run prints of it."""
        if not self.able("the native probe"):
            return
        self.method_allocations()
        sources = os.path.join(self.tests, "godot_native")
        with tempfile.TemporaryDirectory() as where:
            # With the scripts places.gd and emits.gd hold to GDScript under the interpreter: here each is held to
            # itself as native code.
            shared = [os.path.join(self.tests, "godot_project", name) for name in ("Places.as", "Emits.as", "EmitsChild.as", "emits_cases.gd", "Loops.as", "Callables.as", "Kinds.as", "ByName.as", "by_name.gd")]
            project = self.project(where, sources, sorted(os.listdir(sources)) + shared)
            scripts = sorted(name for name in os.listdir(project) if name.endswith(".as"))
            program = self.library("the probe", project, scripts, where)
            if not program:
                return
            if sys.platform == "darwin":
                self.check("native: a closed Play library leaves dyld", retained_on_macos(program) == 0, program)
            said = {}
            for label, library in (("interpreter", None), ("native", program)):
                status, text = self.run(project, "probe.gd", library)
                self.check("native: the probe runs (%s)" % label, status == 0, "exit %d\n%s" % (status, text[-600:]))
                self.check("native: %s" % ("the program library is used" if library else "no library without one"),
                           (USED in text) == bool(library), text[-600:])
                # What the probe gave, and the error Godot was told of with its trace.
                said[label] = [line for line in text.split("\n") if line.startswith("PROBE ") or line.startswith("SCRIPT ERROR") or
                               line.lstrip().startswith("at ") or line.lstrip().startswith("at: ")]
                for case in ("float32 bits", "float32 NaN", "float32 empty", "integer float32 bits",
                             "packed float bits", "packed float NaN", "packed float empty",
                             "float from Godot number", "float from Godot integer", "float inspector default", "float field from Godot",
                             "packed byte values", "packed byte empty", "byte from Godot integer", "byte from Godot number",
                             "byte inspector default", "byte field from Godot", "byte field rejects out of range"):
                    self.check("%s: %s" % (label, case), "PROBE %s=true" % case in said[label], text[-600:])
                self.check("%s: a warmed receiver can be freed" % label,
                           "PROBE freed after warmup=warmed/1009" in said[label], text[-1200:])
                if library:
                    self.counts("the probe", text, os.path.join(sources, "native_counts.txt"), "probe")
            self.by_name_reads(project, None)
            status, text = self.by_name_reads(project, program)
            for name in ("Callable field", "method value", "foreign engine reads", "computed engine methods",
                         "computed getter values keep their types and copies", "parameter alias respects position overrides"):
                pairs = [line.partition("=")[2].split("|") for line in text.splitlines() if line.startswith(f"VALUE {name}=")]
                self.check(f"native: {name} agrees with GDScript", len(pairs) == 1 and len(pairs[0]) == 2 and pairs[0][0] == pairs[0][1], str(pairs))
            for mode in ("callback", "entry", "reload", "source"):
                pair = "[17, 1069]" if mode == "callback" else "[17, 17]"
                self.check(f"native: reload classification {mode}", f"VALUE reload classification {mode}={pair}|{pair}" in text, text[-2400:])
            self.check("native: computed keys report missing properties",
                       "VALUE computed misses=0|0" in text, text[-1200:])
            cached = {line.removeprefix("kas3: cached miss ") for line in text.splitlines()
                      if line.startswith("kas3: cached miss ")}
            self.check("native: a literal miss is cached", "nothing" in cached, str(sorted(cached)))
            self.check("native: computed misses stay out of the literal cache",
                       cached.isdisjoint(f"slot{i}" for i in range(256)), str(sorted(cached)))
            allocations = [int(line.split()[-1]) for line in text.splitlines() if line.startswith("kas3: field allocations ")]
            self.check("native: warmed scalar field lookup allocates nothing", allocations[1:16] == [0] * 15, str(allocations))
            self.check("native: sixteen computed keys stay warm and a hot key survives churn",
                       "VALUE rotating computed names=192|192" in text and allocations[32:] == [0] * 96,
                       str(allocations))
            self.check("native: reloaded fields use the current getter",
                       "VALUE foreign declaration after replacement=[true, \"virtual\", 1]|[true, \"virtual\", 1]" in text and
                       "VALUE typed reload after staging source=[[true, true, \"virtual\", false], 1]|[[true, true, \"virtual\", false], 1]" in text and
                       "VALUE foreign declaration after in-place reload=[[true, true, \"virtual\", false], 1]|[[true, true, \"virtual\", false], 1]" in text, text[-2200:])
            self.check("native: attaching a script ends the fresh Timer proof",
                       "VALUE fresh timer after attaching a script=8.5|8.5" in text, text[-2000:])
            self.check("native: foreign script declarations support literal and computed names",
                       "VALUE foreign declarations=[true, 41, true, 41]|[true, 41, true, 41]" in text, text[-1800:])
            self.check("native: nullable math member reads raise the null-reference error",
                       "VALUE null math receivers=[1009, 1009]|[1009, 1009]" in text, text[-1600:])
            self.check("native: by-name comparisons run in the program library", status == 0 and USED in text
                       and "SCRIPT ERROR" not in text, text[-1200:])
            self.check("native: the probe said something", len(said["interpreter"]) >= 8, "\n".join(said["interpreter"]))
            for ours, theirs in zip(said["native"], said["interpreter"]):
                self.check("native: as the interpreter says it", ours == theirs, "native       %s\ninterpreter  %s" % (ours, theirs))
            self.check("native: as many lines", len(said["native"]) == len(said["interpreter"]),
                       "%d and %d" % (len(said["native"]), len(said["interpreter"])))
            # A program library built for another layout of the runtime is never read past its first word:
            # it is refused, in words, and the interpreter runs the scripts.
            other = self.library("the probe, for another runtime", project, scripts, where, other_layout=True)
            if other:
                status, text = self.run(project, "probe.gd", other)
                refused = [line for line in text.split("\n") if line.startswith("PROBE ")]
                self.check("native: a program library built for another version of the runtime is refused, and says so",
                           status == 0 and USED not in text and OTHER_LAYOUT in text, "exit %d\n%s" % (status, text[-900:]))
                self.check("native: the interpreter runs the scripts then", refused == [line for line in said["interpreter"] if line.startswith("PROBE ")],
                           "\n".join(refused[:5]))

    def workloads(self):
        """Each workload of the languages benchmark as native code, counted."""
        if not self.able("the count of the workloads"):
            return
        sources = os.path.join(self.tests, "bench", "languages")
        listed = os.path.join(sources, "native_counts.txt")
        names = [line.split(": ")[0] for line in open(listed).read().splitlines() if line and not line.startswith("#")]
        with tempfile.TemporaryDirectory() as where:
            # The benchmark's own project is for Godot .NET: the probe's plain one serves here.
            project = self.project(where, sources, ["CmpAs.as", "counts.gd", os.path.join(self.tests, "godot_native", "project.godot")])
            program = self.library("the workloads", project, ["CmpAs.as"], where)
            if not program:
                return
            for name in names:
                status, text = self.run(project, "counts.gd", program, [name, str(ROUNDS)])
                self.check("native: the workload %s runs" % name, status == 0 and USED in text and "COUNTED " + name in text,
                           "exit %d\n%s" % (status, text[-600:]))
                self.counts("the workload %s" % name, text, listed, name)

    def frames(self):
        """Each frame benchmark as native code for a few frames, counted."""
        if not self.able("the count of the frame benchmarks"):
            return
        sources = os.path.join(self.tests, "bench", "languages")
        listed = os.path.join(sources, "frames", "native_counts.txt")
        names = [line.split(": ")[0] for line in open(listed).read().splitlines() if line and not line.startswith("#")]
        with tempfile.TemporaryDirectory() as where:
            project = self.project(where, sources, ["frame_counts.gd", os.path.join(self.tests, "godot_native", "project.godot")])
            scripts = sorted("frames/" + name for name in os.listdir(os.path.join(sources, "frames")) if name.endswith(".as"))
            os.makedirs(os.path.join(project, "frames"))
            for script in scripts:
                shutil.copy(os.path.join(sources, script), os.path.join(project, "frames"))
            program = self.library("the frame benchmarks", project, scripts, where)
            if not program:
                return
            for name in names:
                status, text = self.run(project, "frame_counts.gd", program, [name, str(FRAMES)], ["--fixed-fps", "60"])
                self.check("native: the frame benchmark %s runs" % name, status == 0 and USED in text and "COUNTED " + name in text,
                           "exit %d\n%s" % (status, text[-600:]))
                self.counts("the frame benchmark %s" % name, text, listed, name)
