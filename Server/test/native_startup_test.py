#!/usr/bin/env python3
"""Launch the real Linux client through a rendered startup frame and exit.

Run under Xvfb with a software Vulkan ICD (Mesa lavapipe), for example:
    xvfb-run -a python3 Server/test/native_startup_test.py build/bin/zerospades build/startup-artifacts
The executable's Resources directory must contain the built paks. The CI job
also adds the real Unifont pak, so this exercises PNG decoding and startup font
atlases, not only a fake image device or an empty-glyph unit fixture.
"""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def launch(executable: Path, artifacts: Path, renderer: str) -> None:
    artifacts.mkdir(parents=True, exist_ok=True)
    # Never accept a previous run's log or screenshot as evidence of success.
    run_dir = Path(tempfile.mkdtemp(prefix=f"{renderer}-", dir=artifacts))
    env = os.environ.copy()
    # Keep the check isolated from the runner's/user's real config and mods.
    env["HOME"] = str(run_dir / "home")
    env["XDG_DATA_HOME"] = str(Path(env["HOME"]) / ".local" / "share")
    env["XDG_CONFIG_HOME"] = str(run_dir / "config")
    for key in ("HOME", "XDG_DATA_HOME", "XDG_CONFIG_HOME"):
        Path(env[key]).mkdir(parents=True, exist_ok=True)
    env["LIBGL_ALWAYS_SOFTWARE"] = "1"
    env["ZS_VULKAN_VALIDATION"] = "1"
    args = [str(executable), "--debug-tools", "--screenshot-quit", "2"]
    for setting in (
        "cl_showStartupWindow=0",
        "cg_playerName=StartupCheck",
        "cg_playerNameIsSet=1",
        f"r_renderer={renderer}",
        f"r_vulkan={int(renderer == 'vulkan')}",
        "r_allowSoftwareRendering=1",
        "r_fullscreen=0",
        "r_videoWidth=800",
        "r_videoHeight=600",
        "r_multisamples=0",
        "r_water=1",
        "r_waterInScene=0",
        "cl_fps=15",
        "cg_screenshotFormat=png",
        "s_audioDriver=null",
        "vr_enable=0",
    ):
        args.extend(("--override-cvar", setting))
    output = run_dir / "console.log"
    print(f"Launching real {renderer} client; logs: {run_dir}", flush=True)
    try:
        with output.open("w") as log:
            result = subprocess.run(args, cwd=executable.parent, env=env, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=120, check=False)
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError(f"{renderer} client did not complete its startup frame in 120s") from exc
    text = output.read_text(errors="replace")
    for log in run_dir.rglob("SystemMessages.log"):
        text += "\n" + log.read_text(errors="replace")
    required = ["Pak registered: font-unifont.pak:", "Starting Client Loop",
                "Screenshot taken before quitting:",
                "Quitting after the screenshot (--screenshot-quit)"]
    if renderer == "vulkan":
        required += ["Vulkan debug messenger created", "Vulkan device initialized successfully",
                     "Reflected program 'Shaders/Vulkan/Water.vk.program':",
                     "VulkanWaterRenderer pipeline and descriptors created"]
    if result.returncode != 0:
        raise RuntimeError(f"{renderer} client exited with {result.returncode}; see {output}")
    for marker in required:
        if marker not in text:
            raise RuntimeError(f"{renderer} startup did not reach {marker!r}; see {output}")
    if "switched to OpenGL" in text:
        raise RuntimeError("Vulkan test silently fell back to OpenGL")
    if not any(run_dir.rglob("*.png")):
        raise RuntimeError(f"{renderer} startup produced no screenshot")
    if "Validation Error:" in text:
        raise RuntimeError(f"{renderer} startup reported Vulkan validation errors; see {output}")
    print(f"PASS: native {renderer} startup, fonts, screenshot and clean shutdown", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("artifacts", type=Path)
    parser.add_argument("--renderer", choices=("vulkan", "gl", "sw", "both"), default="both")
    args = parser.parse_args()
    executable = args.executable.resolve()
    if not executable.is_file():
        parser.error(f"executable does not exist: {executable}")
    renderers = ("vulkan", "gl") if args.renderer == "both" else (args.renderer,)
    for renderer in renderers:
        launch(executable, args.artifacts.resolve(), renderer)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
