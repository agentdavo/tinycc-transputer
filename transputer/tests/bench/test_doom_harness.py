#!/usr/bin/env python3
"""Focused checks for Doom extraction and run validation."""
import tempfile
import unittest
from pathlib import Path
import subprocess
import sys

import doom_extract
import doom_validate


class DoomHarnessTests(unittest.TestCase):
    def test_stub_only_definition_and_preserve_other_text(self):
        source = '''/* f() { ignored } */
int f(int x) { const char *s = "}"; if (x) { return 3; } return 4; }
int g(void) { return f(1); }
'''
        result = doom_extract.stub_functions(source, ("f",))
        self.assertIn('int f(int x) { bench_unreachable("f"); }', result)
        self.assertIn('int g(void) { return f(1); }', result)
        self.assertIn('/* f() { ignored } */', result)

    def test_missing_and_duplicate_definitions_rejected(self):
        for source in ('int g(void) { return 0; }',
                       'int f(void) { return 0; } int f(void) { return 1; }'):
            with self.assertRaises(ValueError):
                doom_extract.stub_functions(source, ("f",))

    def test_run_validation_rejects_failed_or_incomplete_runs(self):
        body = ''.join(f'{name} 123 {digest}\n' for name, digest in
                       zip(doom_validate.PHASES, doom_validate.EXPECTED))
        valid = 'S' + body + 'check 0 0\ndone\nH'
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'uart'
            path.write_text(valid)
            self.assertEqual(len(doom_validate.read_run(path)), len(doom_validate.PHASES))
            for bad in (valid[:-1], valid.replace('check 0 0', 'check 1 0'),
                        valid.replace('span 123', 'unexpected Doom function: span\nspan 123'),
                        valid.replace('b6eadcb0', 'bad', 1)):
                path.write_text(bad)
                with self.assertRaises(ValueError):
                    doom_validate.read_run(path)

    def test_different_hashes_between_runs_are_rejected(self):
        body = ''.join(f'{name} 123 {digest}\n' for name, digest in
                       zip(doom_validate.PHASES, doom_validate.EXPECTED))
        valid = 'S' + body + 'check 0 0\ndone\nH'
        with tempfile.TemporaryDirectory() as temp:
            a, b = Path(temp) / 'a', Path(temp) / 'b'
            a.write_text(valid)
            b.write_text(valid.replace('b6eadcb0', '16eadcb0', 1))
            result = subprocess.run([sys.executable, doom_validate.__file__, 't800',
                                     str(a), str(b)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('different hashes', result.stderr)
            a.write_text(b.read_text())
            result = subprocess.run([sys.executable, doom_validate.__file__, 't800',
                                     str(a), str(b)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('differ from the reference', result.stderr)


if __name__ == '__main__':
    unittest.main()
