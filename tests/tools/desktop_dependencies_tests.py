import shutil
import subprocess
import tempfile
import time
import unittest
from pathlib import Path


class DesktopDependenciesTests(unittest.TestCase):
    def test_mime_edit_regenerates_desktop_entry(self):
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory(prefix="hikari desktop dependencies ") as directory:
            source = Path(directory) / "source"
            build = Path(directory) / "build"
            for relative in (
                "cmake/DesktopIntegration.cmake",
                "packaging/linux/mime/hikarisub.xml",
                "packaging/linux/io.github.altqx.HikariSub.desktop.in",
                "packaging/linux/io.github.altqx.HikariSub.metainfo.xml.in",
            ):
                destination = source / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(root / relative, destination)
            (source / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.28)\n'
                'project(DesktopDependencies LANGUAGES NONE)\n'
                'include(cmake/DesktopIntegration.cmake)\n'
                'hikari_generate_desktop_files("0.1.0")\n',
                encoding="utf-8",
            )
            subprocess.run(["cmake", "-G", "Ninja", "-S", str(source), "-B", str(build)], check=True)
            desktop = build / "packaging/io.github.altqx.HikariSub.desktop"
            mime_type = "application/x-hikari-dependency-test"
            self.assertNotIn(mime_type, desktop.read_text(encoding="utf-8"))
            time.sleep(1.1)
            xml = source / "packaging/linux/mime/hikarisub.xml"
            xml.write_text(
                xml.read_text(encoding="utf-8").replace(
                    "</mime-info>", f'<mime-type type="{mime_type}"/>\n</mime-info>'
                ),
                encoding="utf-8",
            )
            subprocess.run(["cmake", "--build", str(build)], check=True)
            self.assertIn(f"{mime_type};", desktop.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
