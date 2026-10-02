import sys, os, subprocess
sys.path.insert(0, os.getcwd())
import build_cl as b

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

target_dir = "D:\\DarkFoxInt\\chk"
os.makedirs(target_dir, exist_ok=True)
pch_file = os.path.join("D:\\DarkFoxInt\\development", "DarkFox-dev.pch")

files = sys.argv[1:]

# каждый файл отдельно, со своим pdb/obj -- иначе "file pdb does not match"
for idx, rel in enumerate(files):
    fo = "/Fo" + target_dir + "\\"
    fd = "/Fd" + os.path.join(target_dir, "c%d.pdb" % idx)
    cmd = [b.CL] + common + [
        "/Yupch/pch.hpp", "/Fp" + pch_file, fo, fd, "/TP",
        os.path.join(os.getcwd(), rel.replace("/", "\\")),
    ]
    r = subprocess.run(cmd, cwd=os.getcwd(), env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = r.stdout.decode("utf-8", "replace")
    # гасим шум про pdb: он есть всегда при параллельной сборке
    lines = [l for l in out.splitlines() if "C2859" not in l]
    body = "\n".join(lines).strip()
    print("=== %s -> exit %d" % (rel, r.returncode))
    if body:
        print(body[-4000:])
