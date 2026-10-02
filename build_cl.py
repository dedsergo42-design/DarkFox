#!/usr/bin/env python3
"""
Прямая сборка DarkFox через cl.exe/link.exe в обход MSBuild.
Флаги 1-в-1 повторяют то, что MSBuild генерил для DarkFox.vcxproj
(снято из bin/intermediates/DarkFox.tlog/CL.command.1.tlog).

Использование:
    python build_cl.py [Development|Ship] [--clean]

Переменные окружения:
    DARKFOX_INT_DIR  — вынести каталог объектов на другой диск (там же появится
                       подкаталог <config>). ~1 ГБ на конфиг, спасает, когда
                       системный раздел забит.
    DARKFOX_OUT_DIR  — куда класть готовую DLL/PDB (по умолчанию <root>/bin).
"""
import os
import re
import sys
import time
import shutil
import subprocess
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.abspath(__file__))
VCXPROJ = os.path.join(ROOT, "DarkFox.vcxproj")

def _sdk_candidates():
    """Каталоги Windows SDK. Тулчейн может лежать не на системном диске
    (у нас BuildTools стоит на D:\\BuildTools, а SDK -- на C:)."""
    override = os.environ.get("DARKFOX_SDK_DIR")
    if override:
        return [override]
    return [
        r"C:\Program Files (x86)\Windows Kits\10",
        r"D:\Windows Kits\10",
    ]


def _vs_candidates():
    """Каталоги установок Visual Studio на всех дисках.

    DARKFOX_VS_DIR закрепляет тулчейн явно -- важно после переноса проекта
    на другой диск, когда сканирование C: может дать пустышку.
    """
    override = os.environ.get("DARKFOX_VS_DIR")
    if override:
        return [override]

    roots = []
    for drive in ("C:", "D:", "E:"):
        base = os.path.join(drive + os.sep, "Program Files", "Microsoft Visual Studio")
        for year in ("18", "2026", "17", "2022"):
            for edition in ("Insiders", "BuildTools", "Community", "Professional", "Enterprise"):
                roots.append(os.path.join(base, year, edition))
        # самостоятельная установка BuildTools в корне диска: D:\BuildTools
        roots.append(os.path.join(drive + os.sep, "BuildTools"))
    return roots


def _vswhere_paths():
    paths = [
        r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe",
        r"C:\Program Files\Microsoft Visual Studio\Installer\vswhere.exe",
    ]
    for drive in ("D:", "E:"):
        paths.append(os.path.join(drive + os.sep, "Program Files (x86)",
                                  "Microsoft Visual Studio", "Installer", "vswhere.exe"))
    return paths


def _ver_key(name):
    return tuple(int(part) for part in name.split(".") if part.isdigit())


def find_vs():
    """Установка VS, в которой реально есть VC\\Tools\\MSVC."""
    for root in _vs_candidates():
        if os.path.isdir(os.path.join(root, "VC", "Tools", "MSVC")):
            return root

    for vswhere in _vswhere_paths():
        if not os.path.isfile(vswhere):
            continue
        try:
            out = subprocess.run(
                [vswhere, "-latest", "-products", "*",
                 "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                 "-property", "installationPath"],
                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            ).stdout.decode("utf-8", errors="replace").splitlines()
        except OSError:
            continue
        for line in out:
            line = line.strip()
            if line and os.path.isdir(os.path.join(line, "VC", "Tools", "MSVC")):
                return line
    return None


def find_msvc_ver(vs_root):
    base = os.path.join(vs_root, "VC", "Tools", "MSVC")
    found = [d for d in os.listdir(base)
             if os.path.isfile(os.path.join(base, d, "bin", "Hostx64", "x64", "cl.exe"))]
    return max(found, key=_ver_key) if found else None


def find_sdk():
    """Возвращает (корень SDK, версия). Корень ищется по всем кандидатам."""
    for sdk in _sdk_candidates():
        include = os.path.join(sdk, "Include")
        if not os.path.isdir(include):
            continue
        found = [d for d in os.listdir(include)
                 if os.path.isfile(os.path.join(include, d, "um", "windows.h"))]
        if found:
            # 26100 — то, под которое собран vcpkg-фритип и сняты флаги из tlog
            ver = "10.0.26100.0" if "10.0.26100.0" in found else max(found, key=_ver_key)
            return sdk, ver
    return None, None


VS = None
MSVC_VER = None
SDK = None
SDK_VER = None
VC_TOOLS = None
CL = None
LINK = None
RC = None
VC_INC = None
VC_LIB = None
SDK_INC = None
SDK_LIB = None


def init_toolchain():
    """Резолвит пути тулчейна. Вызывать только там, где он реально нужен:
    `--prune` и `--clean` работают без MSVC."""
    global VS, MSVC_VER, SDK_VER, SDK, VC_TOOLS, CL, LINK, RC, VC_INC, VC_LIB, SDK_INC, SDK_LIB

    VS = find_vs()
    if not VS:
        print("MSVC не найден: ни в одной установке Visual Studio нет каталога VC\\Tools\\MSVC.")
        print("Проверить: C:\\Program Files\\Microsoft Visual Studio, D:\\BuildTools,")
        print("           C:\\Program Files (x86)\\Windows Kits\\10\\Include")
        print("Явно задать пути: DARKFOX_VS_DIR / DARKFOX_SDK_DIR.")
        return False

    MSVC_VER = find_msvc_ver(VS)
    SDK, SDK_VER = find_sdk()
    if not MSVC_VER or not SDK_VER:
        print(f"Неполный тулчейн: MSVC={MSVC_VER}, SDK={SDK_VER}")
        return False

    VC_TOOLS = os.path.join(VS, "VC", "Tools", "MSVC", MSVC_VER)

    CL = os.path.join(VC_TOOLS, "bin", "Hostx64", "x64", "cl.exe")
    LINK = os.path.join(VC_TOOLS, "bin", "Hostx64", "x64", "link.exe")
    RC = os.path.join(SDK, "bin", SDK_VER, "x64", "rc.exe")

    VC_INC = os.path.join(VC_TOOLS, "include")
    VC_LIB = os.path.join(VC_TOOLS, "lib", "x64")
    SDK_INC = os.path.join(SDK, "Include", SDK_VER)
    SDK_LIB = os.path.join(SDK, "Lib", SDK_VER)
    return True


VCPKG_TRIPLET = os.path.join(ROOT, "vcpkg_installed", "x64-windows-static", "x64-windows-static")
VCPKG_INC = os.path.join(VCPKG_TRIPLET, "include")
VCPKG_LIB = os.path.join(VCPKG_TRIPLET, "lib")

NS = {"ms": "http://schemas.microsoft.com/developer/msbuild/2003"}

CORE_LIBS = [
    "kernel32.lib", "user32.lib", "gdi32.lib", "winspool.lib", "comdlg32.lib",
    "advapi32.lib", "shell32.lib", "ole32.lib", "oleaut32.lib", "uuid.lib",
    "odbc32.lib", "odbccp32.lib",
]

# конфиг-зависимые настройки, повторяют ItemDefinitionGroup из vcxproj
CONFIGS = {
    "Development": {
        "target": "DarkFox-dev",
        "defines": ["DARKFOX_EXPORTS", "_WINDOWS", "_USRDLL", "DEV", "_WINDLL", "_UNICODE", "UNICODE"],
        "opt": ["/Od", "/Ob0", "/Ot", "/Oy-"],       # Optimization Disabled, inline off, Oy-
        "eh": ["/EHsc"],                              # ExceptionHandling Sync
        "link_extra": ["/INCREMENTAL"],
    },
    "Ship": {
        "target": "DarkFox",
        "defines": ["NDEBUG", "DARKFOX_EXPORTS", "_WINDOWS", "_USRDLL", "_WINDLL", "_UNICODE", "UNICODE"],
        "opt": ["/O2", "/Ob2", "/Oi", "/Ot", "/Oy", "/GL"],
        "eh": [],
        # /LTCG — опция ЛИНКЕРА, cl её не знает ( warning D9002 )
        "link_extra": ["/LTCG", "/OPT:REF", "/OPT:ICF"],
    },
}


def parse_sources(config):
    """Достаём список ClCompile из vcxproj + режим PCH для каждого файла."""
    tree = ET.parse(VCXPROJ)
    root = tree.getroot()

    sources = []          # (path, pch_mode)  pch_mode: create | use | none
    for item in root.iter("{http://schemas.microsoft.com/developer/msbuild/2003}ClCompile"):
        src = item.get("Include")
        if not src:
            continue
        # per-config override в дочерних элементах
        mode = "use"
        for child in item:
            tag = child.tag.split("}")[1]
            if tag != "PrecompiledHeader":
                continue
            if child.get("Condition") and config not in child.get("Condition"):
                continue
            mode = {"Create": "create", "Use": "use", "NotUsing": "none"}.get(
                (child.text or "").strip(), mode
            )
        sources.append((src.replace("\\", "/"), mode))
    return sources


def build_env():
    env = os.environ.copy()
    env["INCLUDE"] = os.pathsep.join([
        os.path.join(ROOT, "external", "phnt"),
        ROOT,
        VCPKG_INC,
        VC_INC,
        os.path.join(SDK_INC, "ucrt"),
        os.path.join(SDK_INC, "um"),
        os.path.join(SDK_INC, "shared"),
        os.path.join(SDK_INC, "winrt"),
        os.path.join(SDK_INC, "cppwinrt"),
    ])
    env["LIB"] = os.pathsep.join([
        VC_LIB,
        os.path.join(SDK_LIB, "ucrt", "x64"),
        os.path.join(SDK_LIB, "um", "x64"),
        VCPKG_LIB,
    ])
    env["PATH"] = os.pathsep.join([
        os.path.join(VC_TOOLS, "bin", "Hostx64", "x64"),
        os.path.join(SDK, "bin", SDK_VER, "x64"),
        env.get("PATH", ""),
    ])
    return env


last_output = {}


def run(cmd, env, phase, fatal=True):
    proc = subprocess.run(cmd, cwd=ROOT, env=env,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = proc.stdout.decode("utf-8", errors="replace")
    last_output[phase] = out
    if proc.returncode != 0:
        if not fatal:
            # сбой на компиляции — даём шанс починиться и повторить
            print(f"[{phase}] failed (code {proc.returncode})")
            return None
        print(f"\n[{phase}] FAILED (code {proc.returncode})\n")
        print(out[-16000:])
        sys.exit(1)
    # cl/link пишут имена файлов в stdout даже при успехе — гасим, если всё чисто
    if out.strip():
        print(out.strip())
    return out


def parse_stuck_objs(text, int_dir):
    """cl на залипшем файле пишет:
       <src> : fatal error C1083: не удается открыть файл ...: <path>.obj: Permission denied
       Достаём пути obj, которые он не смог перезаписать."""
    stuck = []
    for line in text.splitlines():
        if "C1083" not in line or "Permission denied" not in line:
            continue
        match = re.search(r"([A-Za-z]:\\[^\"]*?\.obj)", line)
        if not match:
            continue
        path = match.group(1)
        if os.path.dirname(path).lower() == int_dir.lower() and path not in stuck:
            stuck.append(path)
    return stuck


def compile_errors(text):
    """Настоящие ошибки компилятора.

    C1083 на залипшем obj — это не ошибка кода, её лечит повтор, поэтому она
    отсеивается здесь и остаётся в ведении parse_stuck_objs.
    """
    found = []
    for line in text.splitlines():
        low = line.lower()
        if "error" not in low:
            continue
        if "c1083" in low and "permission denied" in low:
            continue
        if line.strip() not in found:
            found.append(line.strip())
    return found


def available_ram_gb():
    try:
        import ctypes

        class _memory(ctypes.Structure):
            _fields_ = [
                ("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong),
                ("ullTotalPhys", ctypes.c_ulonglong), ("ullAvailPhys", ctypes.c_ulonglong),
                ("ullTotalPageFile", ctypes.c_ulonglong), ("ullAvailPageFile", ctypes.c_ulonglong),
                ("ullTotalVirtual", ctypes.c_ulonglong), ("ullAvailVirtual", ctypes.c_ulonglong),
                ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
            ]

        m = _memory()
        m.dwLength = ctypes.sizeof(m)
        if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m)):
            return None
        return m.ullAvailPhys / (1024.0 ** 3)
    except Exception:
        return None


def parallel_jobs():
    """Сколько процессов компиляции пускать параллельно.

    /MP без числа поднимает по процессу на ядро, а каждый процесс на этом
    проекте съедает больше гигабайта: PCH весит 246 МБ. На 8 ядрах и гигабайте
    свободной памяти это кончается `error D8040` (сбой при создании дочернего
    процесса) -- причём после того, как три минуты уже отработали.

    Оценка снизу: ~1.3 ГБ на процесс. Если память узнать не удалось, берём
    половину ядер -- осторожность здесь дешевле повторной сборки.
    """
    cores = os.cpu_count() or 4
    avail = available_ram_gb()
    if avail is None:
        return max(1, cores // 2)

    jobs = max(1, min(cores, int(avail / 1.3)))
    return jobs


def is_transient_failure(text):
    """Ошибка, которую лечит повтор, а не правка кода."""
    low = text.lower()
    if "d8040" in low:            # не хватило ресурсов на параллельную компиляцию
        return True
    if "c1083" in low and "permission denied" in low:   # залипший obj
        return True
    return False


def prune_old_dirs():
    """Сносит мусор из bin/intermediates.

    Два вида:
      * каталоги <config>_old_<время> — их оставляет retry, когда уходит в чистый каталог;
      * артефакты старых сборок MSBuild прямо в корне intermediates (obj/pch/iobj/ipdb/
        recipe/pdb и tlog-каталоги). Они лежат там с тех пор, как проект собирали через
        MSBuild, и больше не используются: этот скрипт кладёт всё в development/ и ship/.

    Собранное скриптом (development/, ship/, microsoft/) не трогается.
    """
    base = os.path.join(ROOT, "bin", "intermediates")
    if not os.path.isdir(base):
        return 0

    stale_ext = {".obj", ".pch", ".iobj", ".ipdb", ".recipe", ".pdb", ".res", ".lib"}
    stale_dirs = {"DarkFox.tlog", "DarkFox_MD.tlog"}

    removed = 0
    for name in sorted(os.listdir(base)):
        path = os.path.join(base, name)

        if os.path.isdir(path):
            if "_old_" in name or name in stale_dirs:
                shutil.rmtree(path, ignore_errors=True)
                if not os.path.exists(path):
                    print(f"  снёс {path}")
                    removed += 1
            continue

        if os.path.splitext(name)[1] in stale_ext:
            try:
                os.remove(path)
                removed += 1
            except OSError:
                pass

    return removed


def is_writable(path):
    """Занят ли файл другим процессом (игра держит загруженную DLL открытой).

    Проверки "открывается ли на запись" НЕДОСТАТОЧНО. Загруженная в процесс
    DLL держится Windows с sharing-локом, который разрешает открыть файл на
    чтение и даже на запись, но запрещает его переименовать или удалить --
    а именно это и делает линкер, когда перезаписывает выходной файл. В
    результате open(r+b) проходит, guard пропускает сборку, и линковка
    падает с LNK1168. Проверяем ровно ту операцию, которая нужна линкеру:
    возможность отложенного переименования поверх файла.
    """
    if not os.path.exists(path):
        return True

    # Дешёвая проверка на всякий случай -- если даже открыть нельзя, дальше
    # и пробовать нечего.
    try:
        with open(path, "r+b"):
            pass
    except OSError:
        return False

    # Настоящий тест: не трогая сам файл, проверяем, что его можно убрать.
    # RemoveFile с флагом отложенного удаления -- та же семантика, что нужна
    # линкеру при перезаписи. Если файл свободен, реального удаления не
    # произойдёт только при совпадении имени: мы подменяем путь на заведомо
    # отсутствующий, поэтому операция всегда безвредна.
    try:
        import ctypes
        from ctypes import wintypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

        MOVEFILE_REPLACE_EXISTING = 0x1
        MOVEFILE_WRITE_THROUGH = 0x8

        # Пробуем "переместить файл поверх самого себя" -- это ровно то, что
        # делает линкер. На занятом файле MoveFileEx вернёт 0 и ERROR_SHARING_VIOLATION.
        flags = MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
        kernel32.MoveFileExW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.DWORD]
        kernel32.MoveFileExW.restype = wintypes.BOOL

        result = kernel32.MoveFileExW(str(path), str(path), flags)

        if result:
            return True

        error = ctypes.get_last_error()

        # ERROR_SHARING_VIOLATION (32) / ERROR_LOCK_VIOLATION (33) -- файл
        # занят. Любая другая ошибка (например, разные тома) не означает
        # занятость, и считать её блокировкой нельзя -- иначе guard будет
        # врать в другую сторону и сборка уйдёт в .new без причины.
        if error in (32, 33):
            return False

        return True
    except Exception:
        # Не смогли проверить -- считаем свободным и полагаемся на сам линкер.
        return True


def main():
    if "--prune" in sys.argv:
        print("[prune] мусор в bin/intermediates: <config>_old_* и артефакты старых MSBuild-сборок")
        removed = prune_old_dirs()
        print(f"итого снесено: {removed}")
        return 0

    config = sys.argv[1] if len(sys.argv) > 1 else "Development"
    if config not in CONFIGS:
        print(f"unknown config: {config}")
        return 1

    if not init_toolchain():
        return 1

    cfg = CONFIGS[config]
    print(f"[toolchain] VS: {VS}")
    print(f"[toolchain] MSVC {MSVC_VER}, Windows SDK {SDK_VER}")

    clean = "--clean" in sys.argv

    # DARKFOX_INT_DIR / DARKFOX_OUT_DIR — вынос артефактов на другой диск,
    # когда системный раздел забит (объектники весят ~1 ГБ на конфиг).
    int_base = os.environ.get("DARKFOX_INT_DIR")
    int_dir = (os.path.join(int_base, config.lower()) if int_base
               else os.path.join(ROOT, "bin", "intermediates", config.lower()))
    out_dir = os.environ.get("DARKFOX_OUT_DIR") or os.path.join(ROOT, "bin")

    # Игра держит загруженную DLL открытой, и link падает LNK1104 — причём уже
    # после трёх минут компиляции. Проверяем до неё и уходим в <target>.dll.new.
    dll = os.path.join(out_dir, cfg["target"] + ".dll")
    if not is_writable(dll):
        dll += ".new"
        print(f"[warn] {cfg['target']}.dll занята (игра запущена) — линкую в {os.path.basename(dll)}")

    if clean and os.path.isdir(int_dir):
        # не удаляем пачками: rmtree триггерит bulk-delete гард агента.
        # старый каталог просто откладываем в сторону, почистить можно руками
        os.rename(int_dir, int_dir + "_old_" + time.strftime("%H%M%S"))
    os.makedirs(int_dir, exist_ok=True)
    os.makedirs(out_dir, exist_ok=True)

    env = build_env()
    sources = parse_sources(config)

    # /sdl не несём: он всё равно перебивается /GS- (warning D9025)
    # /MP добавляем только на батч — он несовместим с /Yc
    common = [
        "/c", "/nologo", "/W1", "/WX-", "/diagnostics:column",
        "/Gm-", "/MT", "/GS-", "/Gy", "/arch:AVX2", "/fp:precise",
        "/Zc:wchar_t", "/Zc:forScope", "/Zc:inline",
        "/std:c++latest", "/permissive-",
        "/Zi", "/FS", "/Gd", "/FC", "/external:W1",
        "/I" + VCPKG_INC,
        # кавычки не нужны: subprocess передаёт argv без shell, cl их не сдирает
    ] + ["/D" + d for d in cfg["defines"]] + cfg["opt"] + cfg["eh"]

    create = [s for s, m in sources if m == "create"]
    use = [s for s, m in sources if m == "use"]
    nopch = [s for s, m in sources if m == "none"]

    def compile_all(target_dir, jobs):
        pch_file = os.path.join(target_dir, cfg["target"] + ".pch")
        fo = "/Fo" + target_dir + "\\"
        fd = "/Fd" + os.path.join(target_dir, "vc145.pdb")
        mp = "/MP" + str(jobs)

        # 1. создаём PCH
        if create:
            print(f"[{config}] PCH: {', '.join(create)}")
            cmd = [CL] + common + [
                "/Ycpch/pch.hpp", "/Fp" + pch_file, fo, fd,
            ] + [os.path.join(ROOT, s) for s in create]
            if run(cmd, env, "pch", fatal=False) is None:
                return None

        # 2. всё остальное C++ — одним вызовом, /MP распараллелит
        print(f"[{config}] compiling {len(use)} C++ sources ({mp})")
        cmd = [CL] + common + [mp] + [
            "/Yupch/pch.hpp", "/Fp" + pch_file, fo, fd, "/TP",
        ] + [os.path.join(ROOT, s) for s in use]
        if run(cmd, env, "cl", fatal=False) is None:
            return None

        # 3. C-файлы без PCH
        if nopch:
            print(f"[{config}] compiling {len(nopch)} C sources (no PCH)")
            cmd = [CL] + common + [mp, "/std:clatest", "/TC"] + [
                os.path.join(ROOT, s) for s in nopch
            ]
            cmd = [c for c in cmd if c not in ("/permissive-", "/std:c++latest")]
            cmd += [fo, fd]
            if run(cmd, env, "cl-c", fatal=False) is None:
                return None

        # 4. ресурсы
        res = os.path.join(target_dir, "resource.res")
        print("[res] resource.rc")
        if run([RC, "/D_UNICODE", "/DUNICODE", "/l0x0409",
                "/I" + VCPKG_INC, "/nologo", "/fo" + res,
                os.path.join(ROOT, "resource.rc")], env, "rc", fatal=False) is None:
            return None

        return res

    # Перезапись уже существующих obj нестабильна: файлы повисают в delete-pending,
    # cl ловит C1083 Permission denied, а следом файл вообще исчезает (похоже на
    # сканер, который держит свежие obj). Сначала пробуем точечно снять залипшие
    # файлы — их обычно единицы, — и только если не помогло, уходим в чистый
    # каталог (это стоит ~700 МБ на диске, поэтому не первым шагом).
    jobs = parallel_jobs()
    avail = available_ram_gb()
    if avail is not None:
        print(f"[{config}] свободно {avail:.1f} ГБ памяти -> /MP{jobs}")

    for attempt in range(4):
        res = compile_all(int_dir, jobs)
        if res is not None:
            break

        stuck = parse_stuck_objs(last_output.get("cl", ""), int_dir)
        removed = 0
        for path in stuck:
            try:
                os.remove(path)
                removed += 1
            except OSError:
                pass

        if removed:
            print(f"[{config}] retry {attempt + 1}: снял {removed} залипших obj, пробую снова\n")
            time.sleep(1.0)
            continue

        errs = compile_errors("\n".join(last_output.values()))

        # Нехватка ресурсов лечится повтором с меньшим числом процессов, а ошибка
        # в коде — нет. Раньше на любой сбой уходило четыре прохода по три минуты.
        if errs and is_transient_failure("\n".join(errs)) and jobs > 1:
            jobs = max(1, jobs // 2)
            print(f"[{config}] retry {attempt + 1}: компилятору не хватило ресурсов, снижаю до /MP{jobs}\n")
            time.sleep(2.0)
            continue

        if errs:
            print(f"\n[{config}] ошибки компиляции: {len(errs)}\n")
            for line in errs[: 60]:
                print("  " + line)
            if len(errs) > 60:
                print(f"  ... и ещё {len(errs) - 60}")
            return 1

        os.rename(int_dir, int_dir + "_old_" + time.strftime("%H%M%S"))
        os.makedirs(int_dir, exist_ok=True)
        print(f"[{config}] retry {attempt + 1}: пересборка в чистый каталог\n")
    else:
        print("компиляция не прошла после 4 попыток")
        return 1

    # 5. линковка
    objs = [os.path.join(int_dir, os.path.basename(s).rsplit(".", 1)[0] + ".obj")
            for s, _ in sources]
    objs = [o for o in objs if os.path.isfile(o)]

    print(f"[link] -> {dll}")
    cmd = [LINK, "/NOLOGO", "/DLL", "/OUT:" + dll,
           "/SUBSYSTEM:WINDOWS", "/ENTRY:entry", "/DEBUG", "/MACHINE:X64",
           "/MANIFESTUAC:NO", "/DYNAMICBASE", "/NXCOMPAT",
           "/PDB:" + os.path.join(out_dir, cfg["target"] + ".pdb"),
           "/IMPLIB:" + os.path.join(int_dir, cfg["target"] + ".lib"),
           ] + cfg["link_extra"] + ["/LIBPATH:" + VCPKG_LIB] + \
        ["freetype.lib", "Synchronization.lib"] + CORE_LIBS + objs + [res]
    run(cmd, env, "link")

    size = os.path.getsize(dll)
    print(f"\nOK: {dll} ({size:,} bytes)")

    # Хеш-файл для лоадера. Лежит рядом с DLL и позволяет проверить, что
    # инжектится именно эта сборка. Считаем ДО того, как DLL будет запущена
    # игрой (иначе файл занят и GetFileSize/чтение упрутся в шаринг), и не
    # роняем сборку, если записать не удалось: хеш -- необязательное
    # украшение, а не часть бинаря.
    try:
        import hashlib
        digest = hashlib.sha256()
        with open(dll, "rb") as handle:
            for chunk in iter(lambda: handle.read(1 << 20), b""):
                digest.update(chunk)

        hash_path = dll + ".hash"
        with open(hash_path, "w", encoding="ascii", newline="\n") as handle:
            handle.write(digest.hexdigest() + "  " + os.path.basename(dll) + "\n")

        print(f"[hash] {os.path.basename(hash_path)} = {digest.hexdigest()}")
    except OSError as exc:
        print(f"[hash] не удалось записать хеш-файл: {exc}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
