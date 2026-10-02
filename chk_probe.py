"""Build and run a probe against the real MSVC toolchain.

`chk2.py` only compiles (never links), which is fine for syntax but useless for
anything that has to actually run. This links a real exe with the same flags and
libraries the project builds with, so a probe can query the live registry
instead of guessing at it from the source.

Usage (from the project root):

    python chk_probe.py .workbuddy-ai/temp/probe_keys.cpp

The probe is compiled WITH the project PCH (/Yu), so it sees exactly the headers
the cheat does. Linking pulls in the vcpkg libs for the same reason. Anything the
probe references from the project that has no library will still need a stub in
the probe itself -- the linker names it clearly when that happens.
"""

import os
import subprocess
import sys

sys.path.insert(0, os.getcwd())
import build_cl as b

BS = chr(92)

cfg = b.CONFIGS["Development"]
b.init_toolchain()
env = b.build_env()

out_dir = os.path.join(os.getcwd(), ".workbuddy-ai", "temp", "probe_build")
os.makedirs(out_dir, exist_ok=True)

common = [
    "/nologo", "/W1", "/WX-", "/diagnostics:column", "/c",
    "/Gm-", "/MT", "/GS-", "/Gy", "/arch:AVX2", "/fp:precise",
    "/Zc:wchar_t", "/Zc:forScope", "/Zc:inline",
    "/std:c++latest", "/permissive-",
    "/Zi", "/FS", "/Gd", "/FC", "/external:W1",
    "/I" + b.VCPKG_INC,
] + ["/D" + d for d in cfg["defines"] if d != "_WINDLL"] + cfg["opt"] + cfg["eh"]

root = os.getcwd()
int_dir = "D:" + BS + "DarkFoxInt" + BS + "development"
pch_file = int_dir + BS + "DarkFox-dev.pch"
pdb_file = int_dir + BS + "vc145.pdb"

if len(sys.argv) < 2:
    print(__doc__)
    sys.exit(2)

src = sys.argv[1]
obj_dir = os.path.join(out_dir, "obj")
os.makedirs(obj_dir, exist_ok=True)

# pch.obj must be part of every link: /Yupch/pch.hpp makes the probe's own
# object reference __vcchk / the PCH state, and the linker refuses to build the
# image without the PCH's own object alongside it (LNK2011 otherwise).
pch_obj = os.path.join(obj_dir, "pch.obj")
exe = os.path.join(out_dir, "probe.exe")
obj = os.path.join(obj_dir, "probe.obj")

pch_cmd = [b.CL] + common + [
    "/Ycpch/pch.hpp",
    "/Fp" + pch_file,
    "/Fo" + pch_obj,
    "/Fd" + pdb_file,
    "/TP",
    root + BS + "pch" + BS + "pch.cpp",
]

print("compiling pch.obj")
r = subprocess.run(pch_cmd, cwd=root, env=env,
                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
out = r.stdout.decode("utf-8", "replace")
if r.returncode != 0:
    print(out[-6000:])
    sys.exit(1)

compile_cmd = [b.CL] + common + [
    "/Yupch/pch.hpp",
    "/Fp" + pch_file,
    "/Fo" + obj,
    "/Fd" + pdb_file,
    "/TP",
    root + BS + src.replace("/", BS),
]

print("compiling", src)
r = subprocess.run(compile_cmd, cwd=root, env=env,
                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
out = r.stdout.decode("utf-8", "replace")
if r.returncode != 0:
    print(out[-6000:])
    sys.exit(1)

link_cmd = [b.LINK, "/NOLOGO", "/OUT:" + exe,
            "/SUBSYSTEM:CONSOLE", "/DEBUG", "/MACHINE:X64",
            "/DYNAMICBASE", "/NXCOMPAT",
            "/INCREMENTAL:NO",
            "/LIBPATH:" + b.VCPKG_LIB] + \
    ["freetype.lib", "Synchronization.lib"] + \
    b.CORE_LIBS + [obj, pch_obj]

print("linking ->", exe)
r = subprocess.run(link_cmd, cwd=root, env=env,
                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
out = r.stdout.decode("utf-8", "replace")
if r.returncode != 0:
    print(out[-8000:])
    sys.exit(1)

print("-" * 50)
run = subprocess.run([exe], cwd=root, stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT)
print(run.stdout.decode("utf-8", "replace"))
sys.exit(run.returncode)
