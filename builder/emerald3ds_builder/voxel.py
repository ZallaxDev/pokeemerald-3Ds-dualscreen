"""Run the port's voxel generators on the tree rebuilt from the ROM.

The generator scripts ship with the release (payload/voxelgen/, the same files
as 3ds_port/scripts in the source tree), with the rebuilt tree as their
repository root. Their outputs must match the CRCs the release was built with;
anything else would be a different game data set than the executable expects.

The steps, their order, the copy of the scripts into the tree and the output
checks are shared. Only the way one step is run differs:

* `run_step_subprocess` runs each script in its own process (desktop and the
  frozen Windows builder): no state can carry over between scripts;
* `run_step_inprocess` runs each script in this interpreter (the web builder:
  Pyodide has no processes). It gives every script a fresh copy of its sibling
  modules, its own argv, working directory and import path, and puts them all
  back afterwards, so a script sees the same clean start as in a new process.

Both run the scripts' own `main()` through their `__main__` guard, so the
command-line scripts keep working unchanged and no generator logic is copied.
"""

from __future__ import annotations

import contextlib
import io
import os
import runpy
import shutil
import subprocess
import sys
import zlib
from pathlib import Path

from .errors import BuilderError

# (script, arguments, output path relative to the tree) in the order the
# generators depend on each other.
STEPS = [
    ("gen_voxel_regions.py", [], "3ds_port/romfs/voxel/regions.bin"),
    ("gen_voxel_sign_masks.py", [], "3ds_port/romfs/voxel/signposts.bin"),
    ("gen_voxel_relief.py", ["--output", "3ds_port/romfs/voxel/relief.bin"], "3ds_port/romfs/voxel/relief.bin"),
    ("gen_voxel_buildings.py", ["--output", "3ds_port/romfs/voxel/buildings.bin"],
     "3ds_port/romfs/voxel/buildings.bin"),
    # not voxel data, but made the same way: the intro's leaves scene, widened
    ("gen_intro_margins.py", ["--output", "3ds_port/romfs/stage/leaves.bin"], "3ds_port/romfs/stage/leaves.bin"),
]

RUNNERS = ("subprocess", "inprocess")


def script_command(script: Path, args: list[str]) -> list[str]:
    """How to run one bundled script: through this very executable when frozen."""
    if getattr(sys, "frozen", False):
        exe = Path(sys.executable)
        console = exe.with_name("emerald3ds-builder-cli" + exe.suffix)
        return [str(console if console.exists() else exe), "--run-script", str(script)] + args
    return [sys.executable, "-B", "-m", "emerald3ds_builder", "--run-script", str(script)] + args


def run_step_subprocess(script: Path, args: list[str], cwd: Path) -> None:
    env = dict(os.environ)
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    here = Path(__file__).resolve().parent.parent
    env["PYTHONPATH"] = str(here) + os.pathsep + env.get("PYTHONPATH", "")
    result = subprocess.run(script_command(script, args), cwd=str(cwd), env=env, capture_output=True, text=True,
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    if result.returncode != 0:
        raise BuilderError("A voxel data generator failed (%s)." % script.name,
                           (result.stderr or result.stdout)[-1500:], code="generator_failed")


def run_step_inprocess(script: Path, args: list[str], cwd: Path) -> None:
    scripts_dir = script.parent
    siblings = {p.stem for p in scripts_dir.glob("*.py")}
    saved_argv, saved_path, saved_cwd = sys.argv[:], sys.path[:], os.getcwd()
    saved_modules = {name: sys.modules.pop(name) for name in list(sys.modules) if name in siblings}
    saved_bytecode = sys.dont_write_bytecode
    output = io.StringIO()
    failure = None
    try:
        sys.argv = [str(script)] + list(args)
        sys.path.insert(0, str(scripts_dir))
        sys.dont_write_bytecode = True
        os.chdir(str(cwd))
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            runpy.run_path(str(script), run_name="__main__")
    except SystemExit as exc:
        if exc.code not in (None, 0):
            failure = str(exc.code)
    except MemoryError:
        raise
    except Exception as exc:  # the generator's own crash, reported like a failed process
        failure = "%s: %s" % (type(exc).__name__, exc)
    finally:
        os.chdir(saved_cwd)
        sys.argv = saved_argv
        sys.path[:] = saved_path
        sys.dont_write_bytecode = saved_bytecode
        for name in [n for n in sys.modules if n in siblings]:
            del sys.modules[name]
        sys.modules.update(saved_modules)
    if failure is not None:
        raise BuilderError("A voxel data generator failed (%s)." % script.name,
                           (output.getvalue() + "\n" + failure)[-1500:], code="generator_failed")


def run_generators(tree: Path, voxelgen: Path, expected: list[dict], progress=None,
                   runner: str = "subprocess") -> dict[str, bytes]:
    if runner not in RUNNERS:
        raise ValueError("unknown generator runner %r" % runner)
    run_step = run_step_inprocess if runner == "inprocess" else run_step_subprocess
    if not voxelgen.is_dir():
        raise BuilderError("The release is incomplete: payload/voxelgen is missing.", code="payload_incomplete")
    port = tree / "3ds_port"
    shutil.copytree(voxelgen, port, dirs_exist_ok=True)
    (port / "romfs" / "voxel").mkdir(parents=True, exist_ok=True)
    steps = [s for s in STEPS if (port / "scripts" / s[0]).exists()]
    for i, (script, args, _) in enumerate(steps):
        run_step(port / "scripts" / script,
                 [a if not a.startswith("3ds_port/") else str(tree / a) for a in args], port)
        if progress:
            progress((i + 1) / len(steps))
    outputs: dict[str, bytes] = {}
    for item in expected:
        rel = item["path"]
        path = port / "romfs" / rel
        if not path.exists():
            raise BuilderError("A voxel data file was not produced: %s" % rel, code="generator_output_missing")
        data = path.read_bytes()
        if len(data) != item["size"] or zlib.crc32(data) & 0xFFFFFFFF != item["crc"]:
            raise BuilderError("A voxel data file does not match this release: %s" % rel,
                               "The generated file differs from the one the game was built with.",
                               code="generator_output_mismatch")
        outputs[rel] = data
    return outputs
