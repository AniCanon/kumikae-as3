"""Play builds the project's program library by itself, for run_godot.py (godot_ext/play_build.cpp): a game
run from its folder finds no library for its scripts, starts on the interpreter, and builds one beside it;
the next run loads it.

These are held, with the probe's project (tests/godot_native), each run by Godot headless:

  twice      a first run says it builds and runs the interpreter; when the library is there, a second run
             is native (by what it says, and by the host's counts), says what the first said of the
             scripts, and builds nothing.
  edit       a script edited between runs: the next run builds again, the one after is native.
  switch     the environment and the project's setting turn it off, and the environment wins.
  compiler   no compiler (an empty PATH): one warning that names what to install, the interpreter runs.
             A compiler that fails: one warning with where its output is, the interpreter runs, and the
             same run again builds nothing. One that writes something that is no library: refused, said.
             The editor, which runs no game, builds nothing. A build another run began is waited for
             while whoever builds it is alive, and begun again when it is not.
  kept       builds that are neither in use nor among the newest are taken away; other files stay.

run_godot.py also runs its reload and restart tests from a native start (reload.gd, restart.gd), which is
what an edit made while a game runs as native code comes to, and holds a game run from a pack to building
nothing.

Everywhere else the tests turn Play's build off (KAS3_NATIVE_PLAY=0, set by run_godot.py for every run it
makes): they hold the interpreter, and native code by libraries they build themselves.
"""
import glob
import os
import stat
import subprocess
import tempfile
import time

import native_godot

SWITCH = "KAS3_NATIVE_PLAY"
BUILDS = "ActionScript: building the scripts as native code"
USED = "ActionScript: the scripts run as native code."
STILL = "ActionScript: the scripts are still being built as native code"
NO_COMPILER = "ActionScript: no C++ compiler was found"
NOT_BUILT = "ActionScript: the scripts were not built as native code"
NOT_USED = "is not used ("
FALLS_BACK = "They ran as native code, which is not reloaded in a running game"
# How long a build is waited for: the machine may be busy.
PATIENCE = 600


def cache(project):
    return os.path.join(project, ".godot", "kas3")


def held(project):
    """Everything in the project's cache, by name."""
    return sorted(os.listdir(cache(project))) if os.path.isdir(cache(project)) else []


def libraries(project):
    return [name for name in held(project) if "kas3_program_" in name and ".tmp." not in name]


def settled(project, seconds=PATIENCE):
    """Waits until no build is going on in the project's cache: True if that came about in time."""
    end = time.time() + seconds
    while time.time() < end:
        if not glob.glob(os.path.join(cache(project), "*.building")) and not glob.glob(os.path.join(cache(project), "*.tmp.*")):
            return True
        time.sleep(0.2)
    return False


def stub(where, name, body):
    """A program that stands for a C++ compiler."""
    path = os.path.join(where, name)
    with open(path, "w") as file:
        file.write("#!/bin/sh\n" + body)
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)
    return path


class Runner:
    def __init__(self, native, check):
        self.native = native  # native_godot.Runner: how a project is made and run.
        self.check = check

    def run(self, project, environment=None, drop=()):
        """The probe run in the project with Play's build on: (status, output, the probe's lines)."""
        saved = dict(os.environ)
        os.environ[SWITCH] = "1"
        os.environ.update(environment or {})
        for name in drop:
            os.environ.pop(name, None)
        try:
            status, text = self.native.run(project, "probe.gd", None)
        finally:
            os.environ.clear()
            os.environ.update(saved)
        return status, text, [line for line in text.split("\n") if line.startswith("PROBE ")]

    def play(self):
        if not self.native.compiler:
            print("godot: Play's build was not run (it needs a C++ compiler)")
            return
        sources = os.path.join(self.native.tests, "godot_native")
        with tempfile.TemporaryDirectory() as where:
            shared = [os.path.join(self.native.tests, "godot_project", name) for name in ("Places.as", "Emits.as", "EmitsChild.as", "emits_cases.gd")]
            project = self.native.project(where, sources, sorted(os.listdir(sources)) + shared)
            try:
                said = self.twice(project)
                self.edit(project, said)
                self.switch(project)
                self.compiler(project, where)
                self.kept(project)
            finally:
                settled(project)  # Nothing is still writing into the folder as it goes.

    def twice(self, project):
        check = self.check
        status, text, first = self.run(project)
        check("play: a first run goes on with the interpreter", status == 0 and USED not in text and len(first) >= 8, "exit %d\n%s" % (status, text[-900:]))
        check("play: a first run says it builds the scripts, once", text.count(BUILDS) == 1 and "WARNING" not in text, text[-900:])
        check("play: the build ends", settled(project))
        built = libraries(project)
        check("play: the library is in the project's .godot, under a name of its build", len(built) == 1 and not glob.glob(os.path.join(cache(project), "*.failed")),
              str(held(project)))
        if len(built) != 1:
            return first
        written = os.stat(os.path.join(cache(project), built[0])).st_mtime_ns
        status, text, second = self.run(project)
        check("play: the next run is native", status == 0 and USED in text and BUILDS not in text and "WARNING" not in text, "exit %d\n%s" % (status, text[-900:]))
        check("play: the next run is native by the host's counts", len([line for line in text.split("\n") if line.startswith("kas3: counts ")]) == 1, text[-600:])
        check("play: native code says what the interpreter said", second == first, "\n".join(second[:5]))
        check("play: a run that finds its library builds nothing", settled(project, 5) and libraries(project) == built and
              os.stat(os.path.join(cache(project), built[0])).st_mtime_ns == written, str(libraries(project)))
        # With nothing said either way, Play builds: this run is native too.
        status, text, _ = self.run(project, drop=(SWITCH,))
        check("play: native code is what a run gets with nothing said", status == 0 and USED in text, text[-900:])
        return first

    def edit(self, project, said):
        check = self.check
        before = libraries(project)
        with open(os.path.join(project, "Entry.as")) as file:
            source = file.read()
        check("play: the script to edit is as the test expects", "public function defaults(" in source)
        with open(os.path.join(project, "Entry.as"), "w") as file:
            file.write(source.replace("public function defaults(", "public function addedByAnEdit():int { return 7; }\n\tpublic function defaults(", 1))
        status, text, lines = self.run(project)
        check("play: after an edit the run builds again, on the interpreter", status == 0 and USED not in text and text.count(BUILDS) == 1 and lines == said,
              "exit %d\n%s" % (status, text[-900:]))
        check("play: the build after an edit ends", settled(project))
        check("play: an edit's library is another file, beside the first", len(libraries(project)) == 2 and set(before) < set(libraries(project)),
              str(libraries(project)))
        status, text, lines = self.run(project)
        check("play: the run after that is native", status == 0 and USED in text and BUILDS not in text and lines == said, "exit %d\n%s" % (status, text[-900:]))

    def switch(self, project):
        check = self.check
        status, text, lines = self.run(project, {SWITCH: "0"})
        check("play: the environment turns it off", status == 0 and USED not in text and BUILDS not in text and "WARNING" not in text and len(lines) >= 8,
              "exit %d\n%s" % (status, text[-900:]))
        settings = os.path.join(project, "project.godot")
        with open(settings) as file:
            original = file.read()
        with open(settings, "a") as file:
            file.write("\n[actionscript]\n\nnative/play=false\n")
        status, text, lines = self.run(project, drop=(SWITCH,))
        check("play: the project's setting turns it off", status == 0 and USED not in text and BUILDS not in text and len(lines) >= 8,
              "exit %d\n%s" % (status, text[-900:]))
        status, text, _ = self.run(project)
        check("play: the environment's word is over the setting's", status == 0 and USED in text, "exit %d\n%s" % (status, text[-900:]))
        with open(settings, "w") as file:
            file.write(original)

    def compiler(self, project, where):
        check = self.check
        with open(os.path.join(project, "Entry.as")) as file:
            source = file.read()
        with open(os.path.join(project, "Entry.as"), "w") as file:
            file.write(source.replace("addedByAnEdit", "addedByAnother", 1))
        before = held(project)
        # The editor runs no game: it builds nothing for scripts it finds no library for.
        opened = subprocess.run([self.native.godot, *native_godot.FIRST_SCAN, "--path", project], capture_output=True, text=True, timeout=300,
                                env=dict(os.environ, **{SWITCH: "1"}))
        check("play: the editor builds nothing", BUILDS not in opened.stdout + opened.stderr and settled(project, 5) and held(project) == before,
              "%s\n%s" % ((opened.stdout + opened.stderr)[-600:], held(project)))
        # No compiler to be found: Godot is started by its whole path, with a PATH that holds nothing.
        status, text, lines = self.run(project, {"PATH": ""}, drop=("KAS3_CXX",))
        check("play: with no compiler, one warning says what to install", status == 0 and text.count(NO_COMPILER) == 1 and
              ("xcode-select --install" in text or "clang or g++" in text), "exit %d\n%s" % (status, text[-900:]))
        check("play: with no compiler, the interpreter runs the scripts and nothing is built", USED not in text and BUILDS not in text and
              len(lines) >= 8 and held(project) == before, "%s\n%s" % (text[-600:], held(project)))
        # A compiler that fails: what it says is kept, and the run that started it is none the worse.
        fails = stub(where, "fails", "echo 'program.cpp:1:1: error: the stub fails' >&2\nexit 1\n")
        status, text, lines = self.run(project, {"KAS3_CXX": fails})
        check("play: a build that will fail starts like any other", status == 0 and text.count(BUILDS) == 1 and len(lines) >= 8, "exit %d\n%s" % (status, text[-900:]))
        check("play: the failed build ends", settled(project))
        failed = glob.glob(os.path.join(cache(project), "*.failed"))
        check("play: a failed build leaves its marker and no library", len(failed) == 1 and sorted(libraries(project)) == sorted(name for name in before if "kas3_program_" in name),
              str(held(project)))
        after = held(project)
        status, text, lines = self.run(project, {"KAS3_CXX": fails})
        log = os.path.join(failed[0][:-len(".failed")], "build.log") if failed else ""
        check("play: a build that failed is one warning with where the compiler's output is", status == 0 and text.count(NOT_BUILT) == 1 and
              log in text and "the C++ compiler failed" in text and "no mistake of the scripts'" in text, "exit %d\n%s" % (status, text[-900:]))
        check("play: the compiler's output is there", os.path.exists(log) and "the stub fails" in open(log).read(), log)
        check("play: the interpreter runs the scripts then, and the failed build is not tried again", USED not in text and BUILDS not in text and
              len(lines) >= 8 and settled(project, 5) and held(project) == after, text[-600:])
        # A build another run began: waited for while whoever builds it is alive, begun again when it is not.
        key = os.path.basename(failed[0])[:-len(".failed")] if failed else "none"
        marker = os.path.join(cache(project), key + ".building")
        ended = subprocess.run(["/bin/sh", "-c", "echo $$"], capture_output=True, text=True)
        for label, process, begins in (("one that is alive", str(os.getpid()), False), ("one that is gone", ended.stdout.strip(), True)):
            if failed:
                os.remove(failed[0])
            with open(marker, "w") as file:
                file.write(process + "\n")
            status, text, lines = self.run(project, {"KAS3_CXX": fails})
            check("play: a build %s is building%s" % (label, " is begun again" if begins else ": the run says so and begins none"),
                  status == 0 and text.count(STILL) == (0 if begins else 1) and text.count(BUILDS) == (1 if begins else 0) and len(lines) >= 8 and
                  "WARNING" not in text, "exit %d\n%s" % (status, text[-900:]))
            check("play: the build %s was building ends" % label, settled(project) if begins else os.path.exists(marker), str(held(project)))
            if not begins:
                os.remove(marker)
                with open(failed[0] if failed else marker, "w") as file:
                    file.write("the C++ compiler failed\n")
        # A compiler that ends well and writes something that is no library.
        junk = stub(where, "junk", 'while [ $# -gt 1 ]; do if [ "$1" = -o ]; then echo junk > "$2"; fi; shift; done\n')
        status, text, _ = self.run(project, {"KAS3_CXX": junk})
        check("play: a build that writes junk starts and ends", status == 0 and text.count(BUILDS) == 1 and settled(project), "exit %d\n%s" % (status, text[-900:]))
        status, text, lines = self.run(project, {"KAS3_CXX": junk})
        check("play: a library that can't be loaded is refused in one warning, and the interpreter runs the scripts", status == 0 and
              text.count(NOT_USED) == 1 and USED not in text and len(lines) >= 8, "exit %d\n%s" % (status, text[-900:]))

    def kept(self, project):
        check = self.check
        # Builds of long ago, a file that is no build's, and then a run that builds.
        old = ["%016x" % number for number in range(1, 7)]
        os.makedirs(cache(project), exist_ok=True)
        for age, key in enumerate(old):
            os.makedirs(os.path.join(cache(project), key))
            library = os.path.join(cache(project), "libkas3_program_%s.dylib" % key)
            with open(library, "w") as file:
                file.write("old")
            for path in (library, os.path.join(cache(project), key)):
                os.utime(path, (1000 + age, 1000 + age))
        with open(os.path.join(cache(project), "notes.txt"), "w") as file:
            file.write("not a build's")
        loaded = [name for name in libraries(project) if name[len("libkas3_program_"):][:16] not in old]
        with open(os.path.join(project, "Entry.as")) as file:
            source = file.read()
        with open(os.path.join(project, "Entry.as"), "w") as file:
            file.write(source.replace("addedByAnother", "addedLast", 1))
        status, text, _ = self.run(project)
        check("play: a run builds with old builds lying about", status == 0 and text.count(BUILDS) == 1 and settled(project), "exit %d\n%s" % (status, text[-900:]))
        names = held(project)
        check("play: builds of long ago are taken away, folder and library", not any(key in name for key in old for name in names), str(sorted(names)))
        check("play: a file that is no build's stays", "notes.txt" in names, str(sorted(names)))
        check("play: the newest builds stay, and the one just made", 2 <= len(libraries(project)) <= 5 and len(libraries(project)) <= len(loaded) + 1,
              "%s\n%s" % (libraries(project), loaded))
        status, text, _ = self.run(project)
        check("play: and the run after is native", status == 0 and USED in text, "exit %d\n%s" % (status, text[-900:]))
