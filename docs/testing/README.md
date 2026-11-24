# Native Linux startup check

`Server/test/native_startup_test.py` runs the **real client**, not a mocked
renderer. It requires an X display (Xvfb works), Mesa lavapipe, the Vulkan
validation layer, the built resource paks and the real Unifont pak. It verifies
the water pipeline is reached, a screenshot is written, no Vulkan validation
errors are reported, and the client exits successfully. It rejects silent
Vulkan-to-OpenGL fallback and isolates all config/output in a fresh directory.

The exact dependency, build, fixture-download and launch commands are in
[`linux-startup.workflow.yml`](linux-startup.workflow.yml). This is a workflow
**recipe**, not an enabled GitHub workflow: this session's GitHub connection
cannot write `.github/workflows`. A maintainer with workflow permission can
install it there. It also serves as a reproducible local recipe; run the
commands in its steps from the checkout root (omit `sudo` when already root).
The fixture is pinned and SHA-256 checked. It is downloaded for testing only,
not added to the repository or release package.

After preparing resources as in that recipe:

```sh
export VK_ICD_FILENAMES="$(find /usr/share/vulkan/icd.d -name '*lvp*.json' -print -quit)"
xvfb-run -a -s '-screen 0 1024x768x24' \
  python3 Server/test/native_startup_test.py build/bin/zerospades build/startup-artifacts
```

The check uses null audio; passing it does not certify physical audio devices,
a vendor's GPU/driver, VR headsets, or in-game mod interactions. Those need
separate device/in-game testing. Do not describe shader/unit tests alone as a
successful native launch.
