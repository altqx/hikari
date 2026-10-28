"""Build/run only this throwaway C++ probe; no downloads or machine changes."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
QT = Path("C:/Qt/6.11.2")
CMAKE = Path("C:/Qt/Tools/CMake_64/bin/cmake.exe")
NINJA = Path("C:/Qt/Tools/Ninja/ninja.exe")
VS = Path("C:/Program Files/Microsoft Visual Studio/18/Community")

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def execute(args, env, log, timeout=180):
    result = subprocess.run([str(a) for a in args], env=env, cwd=HERE,
                            capture_output=True, timeout=timeout)
    log.write_bytes(result.stdout + b"\n" + result.stderr)
    if result.returncode:
        print(log.read_text(encoding="utf-8", errors="replace")[-6000:])
        raise RuntimeError(f"Exit {result.returncode}: {args[0]}; {log}")
    return result

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--kit", choices=["mingw", "msvc"], default="mingw")
    parser.add_argument("--observe", action="store_true")
    args = parser.parse_args()
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    out = HERE / "_run" / (stamp + "-" + args.kit)
    build = HERE / "_build" / args.kit
    out.mkdir(parents=True, exist_ok=True)
    build.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    if args.kit == "mingw":
        prefix = QT / "mingw_64"
        compiler = Path("C:/Qt/Tools/mingw1310_64/bin/g++.exe")
        compiler_dir = compiler.parent
    else:
        prefix = QT / "msvc2022_64"
        setup = VS / "Common7/Tools/VsDevCmd.bat"
        # Read only compiler/linker environment fields from a temporary cmd process.
        command = f'call "{setup}" -arch=x64 -host_arch=x64 >nul && set INCLUDE && set LIB && set PATH'
        # cmd.exe parses this command string itself; Python's argv quoting would escape its quotes.
        setup_result = subprocess.run("cmd.exe /d /s /c " + command,
                                      capture_output=True, timeout=45)
        if setup_result.returncode:
            raise RuntimeError("MSVC environment setup failed: " + setup_result.stderr.decode(errors="replace"))
        for line in setup_result.stdout.decode(errors="replace").splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                if key.upper() in {"INCLUDE", "LIB", "LIBPATH", "PATH"}:
                    env[key.upper()] = value
        compiler = VS / "VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/cl.exe"
        compiler_dir = compiler.parent
    env["PATH"] = os.pathsep.join([str(prefix / "bin"), str(compiler_dir),
                                    str(NINJA.parent), env.get("PATH", "")])
    env["QT_QUICK_CONTROLS_STYLE"] = "Basic"
    env["QT_QUICK_BACKEND"] = "software"
    if args.observe:
        env["QT_QPA_PLATFORM"] = "offscreen"
    configure = [CMAKE, "-S", HERE, "-B", build, "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_CXX_COMPILER={compiler}",
                 f"-DCMAKE_MAKE_PROGRAM={NINJA}", f"-DCMAKE_PREFIX_PATH={prefix}"]
    execute(configure, env, out / "configure.log")
    execute([CMAKE, "--build", build, "--parallel", "2"], env, out / "build.log")
    exe = build / "grid_accessibility.exe"
    provenance = {
        "utc": stamp, "kit": args.kit, "qt_prefix": str(prefix),
        "compiler": str(compiler), "compiler_sha256": sha(compiler),
        "cmake": str(CMAKE), "cmake_sha256": sha(CMAKE),
        "ninja": str(NINJA), "ninja_sha256": sha(NINJA),
        "configure_command": [str(a) for a in configure],
        "source_sha256": {f: sha(HERE / f) for f in ["main.cpp", "Main.qml", "CMakeLists.txt", "run.py"]},
        "executable_sha256": sha(exe),
        "qt_config_sha256": sha(prefix / "lib/cmake/Qt6/Qt6Config.cmake"),
        "scope": "Installed-kit feasibility only; not stateless provisioning, native OS AT or performance proof"
    }
    (out / "provenance.json").write_text(json.dumps(provenance, indent=2)+"\n", encoding="utf-8")
    if args.observe:
        execute([exe, "--observe", "--output", out], env, out / "runtime.log", timeout=45)
        observed = json.loads((out / "observations.json").read_text(encoding="utf-8"))
        print(json.dumps({"output": str(out), "kit": args.kit,
                          "observations": len(observed["observations"]),
                          "matched": observed["all_observations_matched"],
                          "cached_cells": observed["cached_cells_at_end"]}))
    else:
        print("Opening native demo; all state stays in memory. Build logs: " + str(out))
        subprocess.Popen([str(exe)], cwd=HERE, env=env,
                         creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)

if __name__ == "__main__":
    main()
