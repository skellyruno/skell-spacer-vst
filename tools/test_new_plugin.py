#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("new_plugin", ROOT / "tools" / "new_plugin.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class GeneratorTests(unittest.TestCase):
    def test_create(self):
        with tempfile.TemporaryDirectory() as root:
            dest = Path(root) / "skell-compressor"
            module.make_project("Skell Compressor", dest, "SC01")
            self.assertIn("SkellCore::DSP SkellCore::UI", (dest / "CMakeLists.txt").read_text())
            self.assertIn('PLUGIN_CODE SC01', (dest / "CMakeLists.txt").read_text())
            self.assertIn('audio.skell.skellcompressor', (dest / "CMakeLists.txt").read_text())
            self.assertIn(module.DEFAULT_REV, (dest / "CMakeLists.txt").read_text())
            self.assertIn("SkellCompressorAudioProcessor", (dest / "Source" / "PluginProcessor.cpp").read_text())
            self.assertTrue((dest / ".github/workflows/build.yml").exists())
            self.assertFalse((dest / "SkellCore").exists())
            with self.assertRaises(FileExistsError):
                module.make_project("Skell Compressor", dest, "SC01")
    def test_invalid_identity(self):
        with tempfile.TemporaryDirectory() as root:
            dest = Path(root) / "bad"
            for invalid in ["123", "abcde", "x#01", "😼😼😼😼"]:
                with self.assertRaises(ValueError):
                    module.make_project("Skell Test", dest, invalid)
            self.assertFalse(dest.exists())
            with self.assertRaises(ValueError):
                module.make_project("Skell Test", dest, "ST01", core_rev="main")
            self.assertFalse(dest.exists())

if __name__ == "__main__":
    unittest.main()
