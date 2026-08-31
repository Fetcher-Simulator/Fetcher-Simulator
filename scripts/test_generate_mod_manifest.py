from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("generate_mod_manifest.py")


class GenerateModManifestTests(unittest.TestCase):
    def run_generator(self, data_dir: Path, content_list: Path, output: Path) -> dict:
        completed = subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                str(data_dir),
                "--content-list",
                str(content_list),
                "--output",
                str(output),
                "--format",
                "json",
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        return json.loads(output.read_text(encoding="utf-8"))

    def test_openmw_config_prepends_builtin_manifest(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            data_dir = root / "Data Files"
            vfs_dir = root / "resources" / "vfs-mw"
            data_dir.mkdir(parents=True)
            vfs_dir.mkdir(parents=True)
            (data_dir / "Example.esm").write_bytes(b"example")
            (vfs_dir / "builtin.omwscripts").write_bytes(b"builtin")
            config = root / "openmw.cfg"
            config.write_text(
                "data=./resources/vfs-mw\n"
                "data=./Data Files\n"
                "content=Example.esm\n",
                encoding="utf-8",
            )
            output = root / "manifest.json"

            manifest = self.run_generator(data_dir, config, output)
            names = [entry["filename"] for entry in manifest["requiredContentFiles"]]
            self.assertEqual(names, ["builtin.omwscripts", "Example.esm"])

    def test_plain_content_list_does_not_inject_builtin_manifest(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            data_dir = root / "Data Files"
            data_dir.mkdir(parents=True)
            (data_dir / "Example.esm").write_bytes(b"example")
            content_list = root / "content.txt"
            content_list.write_text("Example.esm\n", encoding="utf-8")
            output = root / "manifest.json"

            manifest = self.run_generator(data_dir, content_list, output)
            names = [entry["filename"] for entry in manifest["requiredContentFiles"]]
            self.assertEqual(names, ["Example.esm"])


if __name__ == "__main__":
    unittest.main()
