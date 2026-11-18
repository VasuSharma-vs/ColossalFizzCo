"""Run the opt-in machine visual checks in isolated host/client/late-join processes."""
from pathlib import Path
import argparse
import json
import socket
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--editor", default="C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe")
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--map", default="/Game/__CodexMachineVisualCheck/Lvl_ManualMachineVisual")
    args = parser.parse_args()
    project_root = Path(__file__).resolve().parents[1]
    project = project_root / "ColossalFizzCo.uproject"
    output_root = project_root / "Saved/ManualMachineVisual_20261118"
    output_root.mkdir(parents=True, exist_ok=True)
    directory = output_root / ("Network_" + time.strftime("%H%M%S") + f"_{time.time_ns() % 1000000:06d}")
    directory.mkdir()
    port = args.port
    if not port:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
    processes = {}
    streams = []
    result = {"accepted": False, "directory": str(directory), "port": port, "map": args.map}
    manifest_path = project_root / "Binaries/Win64/UnrealEditor.modules"
    result["module_manifest_before"] = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    (directory / "UnrealEditor.modules").write_bytes(manifest_path.read_bytes())

    def launch(role, url):
        stream = (directory / (role + "_stdout.log")).open("w", encoding="utf-8")
        streams.append(stream)
        command = [args.editor, str(project), url, "-game", "-nullrhi", "-nosound", "-unattended", "-nop4",
                   "-NoLiveCoding", "-DisablePlugins=EditorToolset,ModelContextProtocol,MCPClientToolset",
                   "-CustomConfig=PrototypeOffline", "-NoPrototypeMenu", "-NoSplash", "-stdout",
                   "-ExecCmds=t.maxfps 30", f"-port={port}", "-ManualMachineVisualTest", "-NormalColaTestExitAfter=150",
                   "-MachineVisualDir=" + str(directory), "-MachineVisualRole=" + role,
                   "-UserDir=" + str(directory / (role + "User")), "-abslog=" + str(directory / (role + ".log"))]
        processes[role] = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT,
                                           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        result.setdefault("processes", {})[role] = {"pid": processes[role].pid, "command": command}

    def wait_for(path, roles, timeout=100):
        deadline = time.monotonic() + timeout
        while not path.exists():
            failures = list(directory.glob("*_failed.json"))
            if failures:
                raise RuntimeError("Native assertion failed: " + ", ".join(p.name for p in failures))
            for role in roles:
                if processes[role].poll() is not None:
                    raise RuntimeError(f"{role} exited before {path.name}: {processes[role].returncode}")
            if time.monotonic() >= deadline:
                raise RuntimeError("Timed out waiting for " + path.name)
            time.sleep(.25)

    try:
        launch("server", args.map + "?listen")
        wait_for(directory / "server_ready.json", ["server"])
        launch("client", f"127.0.0.1:{port}")
        wait_for(directory / "client_passed.json", ["server", "client"])
        processes["client"].wait(timeout=30)
        launch("late", f"127.0.0.1:{port}")
        wait_for(directory / "late_passed.json", ["server", "late"])
        wait_for(directory / "server_passed.json", ["server"])
        for process in processes.values():
            process.wait(timeout=30)
        result["exit_codes"] = {role: p.returncode for role, p in processes.items()}
        result["accepted"] = all(p.returncode == 0 for p in processes.values())
        result["evidence"] = {role: json.loads((directory / (role + "_passed.json")).read_text(encoding="utf-8-sig"))
                              for role in ["server", "client", "late"]}
    except Exception as error:
        result["error"] = str(error)
        result["failures"] = {p.name: p.read_text(encoding="utf-8-sig") for p in directory.glob("*_failed.json")}
    finally:
        # These handles belong only to processes started by this run; the open editor is never selected or stopped.
        for process in processes.values():
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=10)
        for stream in streams:
            stream.close()
    result["module_manifest_after"] = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    if result["module_manifest_before"] != result["module_manifest_after"]:
        result["accepted"] = False
        result["error"] = "Module manifest changed during the run; evidence does not establish one build."
    (directory / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    (output_root / "NetworkResult.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if result["accepted"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
