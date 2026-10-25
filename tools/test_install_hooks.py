#!/usr/bin/env python3
"""Keep install-hooks from replacing user-owned commit checks."""

from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


class InstallHooks(unittest.TestCase):
    def test_only_generated_hooks_are_replaced(self):
        generated = b"#!/bin/sh\nexec make lint\n"
        for existing in [None, generated, b"#!/bin/sh\nexec make pre-commit\n",
                         b"#!/bin/sh\nmake lint && make test\n"]:
            with self.subTest(existing=existing), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                shutil.copyfile(Path(__file__).resolve().parents[1] / "Makefile", root / "Makefile")
                env = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
                env.update(GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1")
                subprocess.run(["git", "init", "--quiet"], cwd=root, env=env, check=True)
                hook = root / ".git/hooks/pre-commit"
                if existing is not None:
                    hook.write_bytes(existing)
                result = subprocess.run(["make", "install-hooks"], cwd=root, env=env,
                                        capture_output=True, check=False)
                custom = existing is not None and b"&&" in existing
                self.assertEqual(result.returncode, 2 if custom else 0, result.stderr.decode())
                self.assertEqual(hook.read_bytes(), existing if custom else generated)
                if not custom:
                    self.assertTrue(os.access(hook, os.X_OK))


if __name__ == "__main__":
    unittest.main()
