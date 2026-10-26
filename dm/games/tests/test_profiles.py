"""验证逐游戏隔离、构建校验及配置变更的缓存失效。
Verify per-game isolation, build validation and cache invalidation on profile changes.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import game


class ProfilesTest(unittest.TestCase):
    def setUp(self):
        self.profile = game.load(game.DEFAULT_GAME)

    def test_legacy_d2r_default_and_modules(self):
        with patch.dict(os.environ, {'DM_GAME': ''}):
            self.assertEqual(game.load().id, game.DEFAULT_GAME)
        self.assertEqual(self.profile.words('modules'), ['main', 'sdk'])
        self.assertEqual(self.profile.get('romfs_marker'), 'data/build_manifest.psv')

    def test_unknown_title_and_path_are_rejected(self):
        for name in ('../tools', '0100000000000000'):
            with self.assertRaises((ValueError, FileNotFoundError)):
                game.load(name)

    def test_only_registered_module_builds_are_accepted(self):
        for module, build in self.profile.builds.items():
            self.profile.validate_module(module, build)
        for module, build in (('main', '0' * 32), ('rtld', '0' * 32)):
            with self.assertRaises(ValueError):
                self.profile.validate_module(module, build)

    def test_existing_patch_tables_are_preserved(self):
        patches = self.profile.patches()
        tables = {key: getattr(patches, key) for key in ('CUTS', 'OBS', 'PARAMS')}
        digest = hashlib.sha256(json.dumps(tables, ensure_ascii=False, sort_keys=True).encode()).hexdigest()
        self.assertEqual(digest, '679a3da1ec55cb79a7e9cc5265fac3b0cdd75a1ae8da72c4cb517e8a0a59a4c2')
        build = self.profile.builds['main']
        base = patches.cuts_for(build, 'native', 'main', True)
        self.assertEqual(base, patches.CUTS[build])
        enabled = patches.cuts_for(build, 'touch-run-v4', 'main', True)
        disabled = patches.cuts_for(build, 'touch-run-v4', 'main', False)
        self.assertGreater(len(enabled), len(disabled))
        self.assertGreater(len(disabled), len(base))
        self.assertEqual(patches.cuts_for(self.profile.builds['sdk'], 'touch-run-v4', 'sdk', True), [])
        with self.assertRaises(ValueError):
            patches.cuts_for('0' * 32, 'touch-run-v4', 'main', True)

    def test_checked_in_legacy_header_matches_configuration(self):
        self.assertEqual(self.profile.header(), (self.profile.directory / 'game_profile.h').read_text())

    def test_all_rewrite_inputs_invalidate_cache(self):
        with tempfile.TemporaryDirectory() as temp:
            dm = Path(temp) / 'dm'
            (dm / 'tools').mkdir(parents=True)
            (dm / 'tools/patch_nso.py').write_text('baseline')
            root = dm / 'games'
            shutil.copytree(game.HERE, root, ignore=shutil.ignore_patterns('__pycache__'))
            with patch.object(game, 'HERE', root):
                profile = game.load(game.DEFAULT_GAME, root=root)
                before = profile.rewrite_digest()
                for path in (dm / 'tools/patch_nso.py', root / 'game.py',
                             profile.directory / 'game.tsv', profile.directory / 'patches.py'):
                    original = path.read_bytes()
                    path.write_bytes(original + b'\n')
                    self.assertNotEqual(profile.rewrite_digest(), before, path)
                    path.write_bytes(original)
                    self.assertEqual(profile.rewrite_digest(), before)

    def test_duplicate_identity_and_build_entries_are_rejected(self):
        text = (self.profile.directory / 'game.tsv').read_text()
        for suffix in ('title_id\t0100726014352000\n', 'build\tbad\tbad\tmain=' + '0' * 32 + '\n'):
            with tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                directory = root / game.DEFAULT_GAME
                directory.mkdir()
                (directory / 'game.tsv').write_text(text + suffix)
                with self.assertRaises(ValueError):
                    game.load(game.DEFAULT_GAME, root=root)

    def test_pack_cli_rejects_unknown_game_before_reading_assets(self):
        tool = game.HERE.parent / 'tools/d2rm_android.py'
        result = subprocess.run([sys.executable, str(tool), '--game', '0100000000000000',
                                 '/no/game/files', '/no/output'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn('game.tsv', result.stderr)


if __name__ == '__main__':
    unittest.main()
