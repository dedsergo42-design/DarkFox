"""Per-file compile check for DarkFox.

chk.py stops working once /Zi and a shared PCH are in play: cl refuses to
create the compiler pdb with a name that does not match the one baked into the
PCH, and the message it prints (C2859) is filtered out as noise, so every file
looks like a silent "exit 2".

Two things matter and both are easy to get wrong:

  * /Fd must name the *same* pdb the PCH was created with -- the one sitting
    next to DarkFox-dev.pch in the intermediates directory. Pointing it at a
    scratch pdb makes cl rebuild the PCH state and fail.
  * /Fp must point at that same DarkFox-dev.pch, and /Fo must be a different
    directory, otherwise the object lands on top of the real one.

Usage (from the project root, Windows-style paths):

    python chk2.py core/features/misc/impl/map_scan.cpp core/hooks/impl/cheat.cpp
"""

import sys, os, subprocess, glob

sys.path.insert(0, os.getcwd())
import build_cl as b

BS = chr(92)

cfg = b.CONFIGS["Development"]
b.init_toolchain()
env = b.build_env()

common = [
    "/c", "/nologo", "/W1", "/WX-", "/diagnostics:column",
    "/Gm-", "/MT", "/GS-", "/Gy", "/arch:AVX2", "/fp:precise",
    "/Zc:wchar_t", "/Zc:forScope", "/Zc:inline",
    "/std:c++latest", "/permissive-",
    "/Zi", "/FS", "/Gd", "/FC", "/external:W1",
    "/I" + b.VCPKG_INC,
] + ["/D" + d for d in cfg["defines"]] + cfg["opt"] + cfg["eh"]

root = os.getcwd()
int_dir = "D:" + BS + "DarkFoxInt" + BS + "development"
target_dir = "D:" + BS + "DarkFoxInt" + BS + "chk"
os.makedirs(target_dir, exist_ok=True)

pch_file = int_dir + BS + "DarkFox-dev.pch"
pdb_file = int_dir + BS + "vc145.pdb"

files = sys.argv[1:]
if not files:
    print(__doc__)
    sys.exit(2)

failures = 0

for rel in files:
    # A stale object in the scratch dir is harmless, but a stale pdb from a
    # killed run is not: cl will happily reuse a truncated one and report
    # bogus errors. Start clean.
    for p in glob.glob(target_dir + BS + "*.obj") + glob.glob(target_dir + BS + "*.pdb"):
        try:
            os.remove(p)
        except OSError:
            pass

    cmd = [b.CL] + common + [
        "/Yupch/pch.hpp",
        "/Fp" + pch_file,
        "/Fo" + target_dir + BS,
        "/Fd" + pdb_file,
        "/TP",
        root + BS + rel.replace("/", BS),
    ]
    r = subprocess.run(cmd, cwd=root, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = r.stdout.decode("utf-8", "replace")
    lines = [l for l in out.splitlines() if "C2859" not in l]
    body = "\n".join(lines).strip()
    errs = [l for l in lines if "error C" in l or "fatal error" in l]

    status = "ok" if r.returncode == 0 and not errs else "FAIL"
    if status == "FAIL":
        failures += 1
    print("=== %s -> exit %d, %d error(s) [%s]" % (rel, r.returncode, len(errs), status))
    if errs:
        for l in errs[:40]:
            print("   " + l)
    elif r.returncode != 0:
        print(body[:4000])

sys.exit(1 if failures else 0)
