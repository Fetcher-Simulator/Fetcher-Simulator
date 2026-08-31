from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
SCRIPT = REPO_ROOT / "files" / "server" / "Build-OpenMWServerAuthority.py"


def subrecord(name: bytes, payload: bytes) -> bytes:
    return name + struct.pack("<I", len(payload)) + payload


def record(name: bytes, *subrecords: bytes) -> bytes:
    body = b"".join(subrecords)
    return name + struct.pack("<I", len(body)) + b"\0" * 8 + body


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class BuildOpenMWServerAuthorityTests(unittest.TestCase):
    def make_client(self, root: Path) -> tuple[Path, dict[str, bytes]]:
        client = root / "client"
        resource_vfs = client / "resources" / "vfs-mw"
        data_a = client / "Data A"
        data_b = client / "Data B"
        for directory in (resource_vfs, data_a, data_b):
            directory.mkdir(parents=True)

        (resource_vfs / "scripts").mkdir()
        (resource_vfs / "scripts" / "builtin_extra.lua").write_bytes(b"return 'external'\n")

        expected = {
            "archive": b"TES3-BSA-fixture\x00\x01",
            "lua": b"\xef\xbb\xbf-- exact override bytes\r\nreturn 42\r\n",
            "model": b"new-model-bytes\x00\x02",
            "icon": b"new-icon-dds-bytes\x00\x03",
            "collision_model": b"placed-collision-model\x00\x04",
        }
        (data_a / "Morrowind.bsa").write_bytes(expected["archive"])
        (data_a / "scripts").mkdir()
        (data_a / "scripts" / "test.lua").write_bytes(b"old lua\n")
        (data_a / "meshes").mkdir()
        (data_a / "meshes" / "example.nif").write_bytes(b"old model")

        (data_b / "scripts").mkdir()
        (data_b / "scripts" / "test.lua").write_bytes(expected["lua"])
        (data_b / "meshes").mkdir()
        (data_b / "meshes" / "example.nif").write_bytes(expected["model"])
        (data_b / "icons").mkdir()
        (data_b / "icons" / "example.dds").write_bytes(expected["icon"])
        (data_b / "meshes" / "world").mkdir()
        (data_b / "meshes" / "world" / "thebigm.nif").write_bytes(expected["collision_model"])
        (data_b / "meshes" / "world" / "unused.nif").write_bytes(b"unplaced-static-model")

        items = b"".join(
            [
                record(
                    b"WEAP",
                    subrecord(b"NAME", b"fixture_weapon\0"),
                    subrecord(b"MODL", b"example.nif\0"),
                    subrecord(b"ITEX", b"example.tga\0"),
                ),
                record(
                    b"STAT",
                    subrecord(b"NAME", b"TheBigM\0"),
                    subrecord(b"MODL", b"world\\TheBigM.nif\0"),
                ),
                record(
                    b"STAT",
                    subrecord(b"NAME", b"UnusedStatic\0"),
                    subrecord(b"MODL", b"world\\unused.nif\0"),
                ),
                record(
                    b"CELL",
                    subrecord(b"NAME", b"Fixture Cell\0"),
                    subrecord(b"FRMR", struct.pack("<I", 1)),
                    subrecord(b"NAME", b"TheBigM\0"),
                ),
            ]
        )
        (data_a / "Items.esp").write_bytes(items)
        (data_b / "Test.omwscripts").write_bytes(b"PLAYER: scripts/test.lua\r\n")

        cfg = client / "openmw.cfg"
        cfg.write_text(
            "\n".join(
                [
                    'data="./resources/vfs-mw"',
                    'data="./Data A"',
                    'data="./Data B"',
                    "fallback-archive=Morrowind.bsa",
                    "fallback=LightAttenuation_UseConstant,0",
                    "content=Items.esp",
                    "content=Test.omwscripts",
                    "",
                ]
            ),
            encoding="utf-8",
            newline="\n",
        )
        return client, expected

    def run_builder(self, client: Path, output: Path, *extra: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                "--client-root",
                str(client),
                "--output",
                str(output),
                *extra,
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
        )

    def test_build_preserves_effective_bytes_and_generates_portable_config(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            client, expected = self.make_client(root)
            output = root / "content-authority"

            result = self.run_builder(client, output)
            self.assertEqual(result.returncode, 0, msg=result.stderr + result.stdout)

            content_data = output / "content-data"
            self.assertEqual((content_data / "Morrowind.bsa").read_bytes(), expected["archive"])
            self.assertEqual((content_data / "scripts" / "test.lua").read_bytes(), expected["lua"])
            self.assertEqual((content_data / "meshes" / "example.nif").read_bytes(), expected["model"])
            self.assertEqual((content_data / "icons" / "example.dds").read_bytes(), expected["icon"])
            self.assertEqual(
                (content_data / "meshes" / "world" / "thebigm.nif").read_bytes(),
                expected["collision_model"],
            )
            self.assertFalse((content_data / "meshes" / "world" / "unused.nif").exists())
            self.assertFalse((content_data / "scripts" / "builtin_extra.lua").exists())

            generated_cfg = (output / "openmw.cfg").read_text(encoding="utf-8")
            self.assertIn('data="../resources/vfs-mw"', generated_cfg)
            self.assertIn('data="./content-data"', generated_cfg)
            self.assertIn("fallback-archive=Morrowind.bsa", generated_cfg)
            self.assertLess(generated_cfg.index("content=Items.esp"), generated_cfg.index("content=Test.omwscripts"))

            manifest = json.loads((output / "bundle-manifest.json").read_text(encoding="utf-8"))
            files = {entry["path"].casefold(): entry for entry in manifest["files"]}
            self.assertEqual(files["scripts/test.lua"]["sha256"], sha256(expected["lua"]))
            self.assertEqual(files["meshes/example.nif"]["sha256"], sha256(expected["model"]))
            self.assertEqual(files["icons/example.dds"]["sha256"], sha256(expected["icon"]))
            self.assertEqual(
                files["meshes/world/thebigm.nif"]["sha256"], sha256(expected["collision_model"])
            )
            self.assertIn("collision-model", files["meshes/world/thebigm.nif"]["categories"])
            self.assertEqual(manifest["content_count"], 2)
            self.assertEqual(manifest["archive_count"], 1)
            self.assertEqual(manifest["model_refs"], 1)
            self.assertEqual(manifest["icon_refs"], 1)
            self.assertEqual(manifest["unresolved_model_count"], 0)
            self.assertEqual(manifest["unresolved_icon_count"], 0)
            self.assertEqual(manifest["placed_reference_ids"], 1)
            self.assertEqual(manifest["collision_model_refs"], 1)
            self.assertEqual(manifest["unresolved_collision_model_count"], 0)

    def test_plan_only_does_not_require_output(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            client, _ = self.make_client(root)
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--client-root",
                    str(client),
                    "--plan-only",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, msg=result.stderr + result.stdout)
            self.assertIn('"planned_bundle_files"', result.stdout)

    def test_server_vfs_mw_override_is_written_verbatim(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            client, _ = self.make_client(root)
            output = root / "content-authority"
            server_vfs_mw = "/usr/share/games/openmw/resources/vfs-mw"

            result = self.run_builder(client, output, "--server-vfs-mw", server_vfs_mw)
            self.assertEqual(result.returncode, 0, msg=result.stderr + result.stdout)
            generated_cfg = (output / "openmw.cfg").read_text(encoding="utf-8")
            self.assertIn(f'data="{server_vfs_mw}"', generated_cfg)
            manifest = json.loads((output / "bundle-manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["server_vfs_mw"], server_vfs_mw)

    def test_existing_output_requires_force(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            client, _ = self.make_client(root)
            output = root / "content-authority"
            self.assertEqual(self.run_builder(client, output).returncode, 0)
            marker = output / "old-marker.txt"
            marker.write_text("old", encoding="utf-8")

            without_force = self.run_builder(client, output)
            self.assertNotEqual(without_force.returncode, 0)
            self.assertTrue(marker.exists())

            with_force = self.run_builder(client, output, "--force")
            self.assertEqual(with_force.returncode, 0, msg=with_force.stderr + with_force.stdout)
            self.assertFalse(marker.exists())
            self.assertTrue((output / "bundle-manifest.json").is_file())


if __name__ == "__main__":
    unittest.main()
