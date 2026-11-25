#!/usr/bin/env python3
"""Exercise the production thumbnail VM/mesher/rasterizer from Python.

Offline by default; it creates small ZIP/KV6/script fixtures and tests all stock
weapons. --packs-dir adds EVERY pinned official pak and attachment/sight/charm
checks. --fetch explicitly downloads that corpus (never part of normal CTest).
No Pillow, window, GPU, game process or third-party Python packages required.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import unittest
import urllib.request
import zipfile


ARGS = None
MANIFEST = Path(__file__).with_name("fixtures") / "skin_preview_packs.json"


def kv6(color=0x406080, pivot=(0.0, 0.0, 0.0)):
    # Internal voxel colors are R,G,B bytes; a KV6 stores B,G,R bytes.
    stored = (color & 255) << 16 | (color & 0xFF00) | (color >> 16 & 255)
    return (struct.pack("<4sIIIfffI", b"Kvxl", 1, 1, 1, *pivot, 1)
            + struct.pack("<IHBB", stored, 0, 63, 0)
            + struct.pack("<IH", 1, 1))


def skin(draw, fields="", construct=""):
    return """namespace spades {
        class ViewSMGSkin : BasicViewWeapon {
            %s
            ViewSMGSkin(Renderer@ r, AudioDevice@ dev) { super(r); %s }
            void AddToScene() { %s }
        }
        IWeaponSkin@ CreateViewSMGSkin(Renderer@ r, AudioDevice@ dev) {
            return ViewSMGSkin(r, dev);
        }
    }""" % (fields, construct, draw)


class PreviewTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="xrspades-preview-")
        cls.root = Path(cls.temp.name)
        cls.serial = 0

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_preview(self, kind="smg", pak=None, settings=None, success=True, label=None):
        type(self).serial += 1
        prefix = self.root / (label or f"preview-{self.serial}")
        if label and ARGS.artifacts:
            prefix = ARGS.artifacts / label
        command = [str(ARGS.fixture), str(ARGS.resources), kind, str(pak) if pak else "-", str(prefix)]
        command.extend(f"{key}={value}" for key, value in (settings or {}).items())
        result = subprocess.run(command, text=True, capture_output=True, timeout=15)
        output = result.stdout + result.stderr
        if not success:
            self.assertNotEqual(result.returncode, 0, output)
            self.assertFalse(prefix.with_suffix(".json").exists(), output)
            return output
        self.assertEqual(result.returncode, 0, output)
        metadata = json.loads(prefix.with_suffix(".json").read_text())
        self.assertTrue(metadata["scripted"], output)
        self.assertGreater(metadata["faces"], 0)
        self.assertEqual(len(metadata["coverage"]), 24)
        for coverage in metadata["coverage"]:
            self.assertGreater(coverage, 5, "weapon vanished while rotating")
            self.assertLess(coverage, metadata["width"] * metadata["height"] * .9,
                            "assembly is not framed inside its card")
        for part in metadata["parts"]:
            self.assertEqual(len(part["matrix"]), 16)
            self.assertTrue(all(math.isfinite(v) for v in part["matrix"] + part["color"]))
            self.assertTrue(all(0 < v <= limit for v, limit in zip(part["dimensions"], (512, 512, 64))))
        images = []
        for angle in (0, 1, 6, 12, 18):
            pixels = Path(f"{prefix}-{angle}.rgba").read_bytes()
            self.assertEqual(len(pixels), metadata["width"] * metadata["height"] * 4)
            self.assertTrue(any(0 < a < 255 for a in pixels[3::4]), "missing antialiased edges")
            images.append(pixels)
        self.assertNotEqual(images[0], images[1], "thumbnail does not actually rotate")
        return metadata, images

    def pak(self, source, extra=None):
        type(self).serial += 1
        path = self.root / f"SMG-Test-{self.serial}.pak"
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("Scripts/Skin/SMG/View.as", source)
            for name, data in (extra or {}).items():
                archive.writestr(name, data)
        return path

    def test_all_five_stock_assemblies_and_sight_alternatives(self):
        expected = {
            "smg": ["weaponnomagazine.kv6", "magazine.kv6", "sightrear.kv6", "sightfrontpin.kv6", "sightfront.kv6"],
            "rifle": ["weaponnomagazine.kv6", "magazine.kv6", "sightrear.kv6", "sightfront.kv6"],
            "shotgun": ["weaponnopump.kv6", "pump.kv6", "sightrear.kv6", "sightfront.kv6"],
            "spade": ["spade.kv6"], "grenade": ["grenade.kv6"],
        }
        for kind, parts in expected.items():
            for reflex in range(4):
                for charms in (0, 1):
                    with self.subTest(kind=kind, reflex=reflex, charms=charms):
                        scene, _ = self.run_preview(kind, settings={"cg_reflexScope": reflex, "cg_weaponCharms": charms},
                                                    label=f"Default-{kind}-r{reflex}-c{charms}")
                        names = [Path(p["path"]).name.lower() for p in scene["parts"]]
                        expected_parts = list(parts)
                        if reflex and "sightrear.kv6" in expected_parts:
                            expected_parts.remove("sightrear.kv6")
                            expected_parts.append("sightreflex.kv6")
                        if charms and kind in ("smg", "rifle", "shotgun"):
                            expected_parts += ["charmbase.kv6", "charm.kv6"]
                        self.assertCountEqual(names, expected_parts)

    def test_actual_draws_not_all_registered_models(self):
        source = skin("""
            ModelRenderParam p;
            p.matrix = CreateTranslateMatrix(1, 2, 3) * CreateScaleMatrix(2, 3, 4);
            p.customColor = teamColor;
            renderer.AddModel(body, p);
            p.matrix *= CreateTranslateMatrix(0, 2, 0);
            p.customColor = Vector3(0.8F, 0.1F, 0.3F);
            renderer.AddModel(reloading ? empty : full, p);
            if (optic.BoolValue) renderer.AddModel(sight, p);
            // Registered casing and empty magazine must NOT appear in this idle pose.
        """, fields="""
            Model@ body, full, empty, sight, casing;
            ConfigItem optic("preview_optic", "0");
        """, construct="""
            @body = r.RegisterModel("Models/Weapons/SMG/Body.kv6");
            @full = r.RegisterModel("Models/Weapons/SMG/Full.kv6");
            @empty = r.RegisterModel("Models/Weapons/SMG/Empty.kv6");
            @sight = r.RegisterModel("Models/Weapons/SMG/Optic.kv6");
            @casing = r.RegisterModel("Models/Weapons/SMG/CasingNeverLoaded.kv6");
        """)
        pak = self.pak(source, {f"Models/Weapons/SMG/{name}.kv6": kv6(pivot=(3, -5, 8))
                                for name in ("Body", "Full", "Optic")})
        scene, _ = self.run_preview(pak=pak, settings={"preview_optic": 1, "team": ".1,.5,.9"})
        self.assertEqual([Path(p["path"]).name for p in scene["parts"]], ["Body.kv6", "Full.kv6", "Optic.kv6"])
        body, magazine, _ = scene["parts"]
        self.assertEqual(body["matrix"], [2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 1, 2, 3, 1])
        self.assertEqual(magazine["matrix"][12:15], [1, 8, 3])
        self.assertEqual(body["origin"], [-3, 5, -8])
        for value, expected in zip(body["color"], (.1, .5, .9)):
            self.assertAlmostEqual(value, expected, places=6)
        for value, expected in zip(magazine["color"], (.8, .1, .3)):
            self.assertAlmostEqual(value, expected, places=6)
        other, _ = self.run_preview(pak=pak, settings={"preview_optic": 0})
        self.assertEqual(len(other["parts"]), 2)

    def test_config_writes_are_local_and_audio_is_inert(self):
        source = skin("""
            ConfigItem original("cg_fov");
            ConfigItem changed("cg_fov");
            changed.FloatValue = 200;
            ConfigItem again("cg_fov");
            ModelRenderParam p;
            p.matrix = CreateTranslateMatrix(original.FloatValue, changed.FloatValue, again.FloatValue);
            renderer.AddModel(model, p);
        """, fields="Model@ model;", construct="""
            @model = r.RegisterModel("Models/Weapons/SMG/Test.kv6");
            AudioParam p;
            dev.PlayLocal(dev.RegisterSound("DoesNotExist.opus"), p);
        """)
        pak = self.pak(source, {"Models/Weapons/SMG/Test.kv6": kv6()})
        scene, _ = self.run_preview(pak=pak, settings={"cg_fov": 90})
        self.assertEqual(scene["parts"][0]["matrix"][12:15], [90, 200, 90])

    def test_preview_random_is_repeatable(self):
        source = skin("""
            ModelRenderParam p;
            p.matrix = CreateTranslateMatrix(GetRandom(), float(GetRandom(100)),
                                             float(GetRandom(int(-8), int(8))));
            renderer.AddModel(model, p);
        """, fields="Model@ model;", construct="""
            @model = r.RegisterModel("Models/Weapons/SMG/Test.kv6");
        """)
        pak = self.pak(source, {"Models/Weapons/SMG/Test.kv6": kv6()})
        first, images = self.run_preview(pak=pak)
        second, again = self.run_preview(pak=pak)
        self.assertEqual(first["parts"], second["parts"])
        self.assertEqual(images, again)
        x, y, z = first["parts"][0]["matrix"][12:15]
        self.assertTrue(0 <= x < 1 and 0 <= y < 100 and -8 <= z < 8)

    def test_procedural_parts_survive_private_vm_destruction(self):
        source = skin("""
            VoxelModel voxel(1, 1, 1);
            voxel.SetSolid(0, 0, 0, 0x2030F0);
            ModelRenderParam p;
            renderer.AddModel(renderer.CreateModel(voxel), p);
        """)
        scene, _ = self.run_preview(pak=self.pak(source))
        self.assertEqual(scene["parts"][0]["dimensions"], [1, 1, 1])

    def test_unsafe_or_bad_scripts_never_return_partial_scenes(self):
        cases = {
            "loop": skin("while (true) {}"),
            "no-file-api": skin('VoxelModel voxel(1,1,1); voxel.Save("preview-escape.kv6");'),
            "no-game-api": skin('FileManager::ReadAllBytes("SelectedSkins.txt");'),
            "include-escape": '#include "../../../../../../outside.as"',
            "missing-part": skin('ModelRenderParam p; renderer.AddModel(renderer.RegisterModel("Models/Missing.kv6"), p);'),
            "draw-limit": skin('ModelRenderParam p; for (int i=0; i<129; ++i) renderer.AddModel(renderer.RegisterModel("Models/Weapons/SMG/Test.kv6"), p);'),
            "voxel-limit": skin('VoxelModel voxel(1000000,1000000,64);'),
            "path-escape": skin('ModelRenderParam p; renderer.AddModel(renderer.RegisterModel("../../../../etc/passwd.kv6"), p);'),
            "source-limit": "/" * (512 * 1024 + 1),
        }
        for name, source in cases.items():
            with self.subTest(name=name):
                start = time.monotonic()
                self.run_preview(pak=self.pak(source), success=False)
                self.assertLess(time.monotonic() - start, 5, "preview did not enforce its execution limit")
        self.assertFalse(Path("preview-escape.kv6").exists())

    def test_malformed_model_headers_are_bounded(self):
        source = skin('ModelRenderParam p; renderer.AddModel(renderer.RegisterModel("Models/Weapons/SMG/Test.kv6"), p);')
        for data in (b"not a KV6", kv6()[:35],
                     struct.pack("<4sIIIfffI", b"Kvxl", 0x7FFFFFFF, 0x7FFFFFFF, 64, 0, 0, 0, 1)):
            with self.subTest(data=data[:20]):
                self.run_preview(pak=self.pak(source, {"Models/Weapons/SMG/Test.kv6": data}), success=False)

    def test_every_official_pack(self):
        if not ARGS.packs_dir:
            self.skipTest("optional pinned corpus; use --packs-dir (and --fetch to download)")
        manifest = json.loads(MANIFEST.read_text())
        self.assertEqual(len(manifest["packs"]), 18)
        tested = 0
        for entry in manifest["packs"]:
            pak = ARGS.packs_dir / entry["name"]
            with self.subTest(pak=entry["name"]):
                self.assertTrue(pak.is_file(), f"missing pinned fixture: {pak}")
                self.assertEqual(hashlib.sha256(pak.read_bytes()).hexdigest(), entry["sha256"])
                with zipfile.ZipFile(pak) as archive:
                    self.assertIsNone(archive.testzip(), "corrupt pak")
                    self.assertTrue(archive.namelist())
            if "type" not in entry:
                continue  # Fonts/SFX have no voxel assembly; still check archive/hash.
            tested += 1
            for reflex in range(4):
                for charms in (0, 1):
                    with self.subTest(pak=entry["name"], reflex=reflex, charms=charms):
                        scene, _ = self.run_preview(entry["type"], pak,
                            {"cg_reflexScope": reflex, "cg_weaponCharms": charms},
                            label=f"{pak.stem}-r{reflex}-c{charms}")
                        names = [Path(p["path"]).name.lower() for p in scene["parts"]]
                        expected = entry.get("reflex_parts", entry["parts"]) if reflex else entry["parts"]
                        expected = [p.lower() for p in expected]
                        if charms and entry["type"] != "spade":
                            expected += ["charmbase.kv6", "charm.kv6"]
                        self.assertCountEqual(names, expected,
                                              "missing attachment or alternate/reload part drawn")
                        self.assertEqual(scene["beams"], entry.get("beams", 0))
        self.assertEqual(tested, 13, "every official weapon must be exercised")


def fetch_packs(destination):
    """Explicit opt-in only; use an immutable revision and verify every payload."""
    manifest = json.loads(MANIFEST.read_text())
    destination.mkdir(parents=True, exist_ok=True)
    url = f"https://api.github.com/repos/{manifest['repository']}/zipball/{manifest['revision']}"
    request = urllib.request.Request(url, headers={"User-Agent": "XRSpades-preview-tests"})
    with urllib.request.urlopen(request, timeout=60) as response, tempfile.TemporaryFile() as data:
        total = 0
        while chunk := response.read(1024 * 1024):
            total += len(chunk)
            if total > 64 * 1024 * 1024:
                raise ValueError("fixture archive exceeds 64 MiB")
            data.write(chunk)
        data.seek(0)
        with zipfile.ZipFile(data) as archive:
            members = {Path(n).name: n for n in archive.namelist() if n.endswith(".pak")}
            for entry in manifest["packs"]:
                info = archive.getinfo(members[entry["name"]])
                if info.file_size != entry["bytes"]:
                    raise ValueError(f"unexpected fixture size: {entry['name']}")
                payload = archive.read(info)
                if hashlib.sha256(payload).hexdigest() != entry["sha256"]:
                    raise ValueError(f"fixture hash mismatch: {entry['name']}")
                (destination / entry["name"]).write_bytes(payload)


def main():
    global ARGS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("resources", type=Path)
    parser.add_argument("--packs-dir", type=Path)
    parser.add_argument("--fetch", action="store_true")
    parser.add_argument("--artifacts", type=Path, help="keep official/stock RGBA frames and JSON outside Git")
    ARGS = parser.parse_args()
    ARGS.fixture = ARGS.fixture.resolve()
    ARGS.resources = ARGS.resources.resolve()
    if ARGS.fetch:
        if not ARGS.packs_dir:
            parser.error("--fetch requires --packs-dir")
        fetch_packs(ARGS.packs_dir)
    unittest.main(argv=[__file__], verbosity=2)


if __name__ == "__main__":
    main()
