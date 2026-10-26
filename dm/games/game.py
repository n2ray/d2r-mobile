#!/usr/bin/env python3
"""按 Title ID 读取逐游戏配置，生成宿主配置并校验改写输入。
Read per-title profiles, generate host configuration and validate rewrite inputs.
"""
from __future__ import annotations
from dataclasses import dataclass
import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import re
from types import ModuleType

HERE = Path(__file__).resolve().parent
DEFAULT_GAME = '0100726014352000'


@dataclass(frozen=True)
class Game:
    directory: Path
    fields: dict[str, str]
    builds: dict[str, str]

    @property
    def id(self) -> str:
        return self.directory.name

    def get(self, key: str) -> str:
        return self.fields[key]

    def words(self, key: str) -> list[str]:
        return self.get(key).split()

    def integer(self, key: str) -> int:
        return int(self.get(key), 0)

    def validate_module(self, name: str, build_id: str) -> None:
        if name not in self.builds or self.builds[name] != build_id:
            raise ValueError(f'{self.id}: unsupported {name} build {build_id}')

    def patches(self) -> ModuleType:
        path = self.directory / 'patches.py'
        spec = importlib.util.spec_from_file_location(f'game_patches_{self.id}', path)
        if spec is None or spec.loader is None:
            raise ValueError(f'cannot load game patches: {path}')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def rewrite_digest(self) -> str:
        digest = hashlib.sha256()
        for path in (HERE.parent / 'tools/patch_nso.py', HERE / 'game.py',
                     self.directory / 'game.tsv', self.directory / 'patches.py'):
            data = path.read_bytes()
            digest.update(path.relative_to(HERE.parent).as_posix().encode() + b'\0')
            digest.update(len(data).to_bytes(8, 'little'))
            digest.update(data)
        return digest.hexdigest()

    def header(self) -> str:
        lines = ['/* Generated from game.tsv; no game content. 从配置生成，不含游戏内容。 */',
                 '#pragma once', f'#define DM_GAME_TITLE_ID "{self.id}"',
                 f'#define DM_GAME_PROGRAM_ID 0x{self.get("program_id")}ULL']
        for key in ('host_argv0',):
            value = self.get(key)
            if not re.fullmatch(r'[A-Za-z0-9_.-]+', value):
                raise ValueError(f'unsafe C string: {key}')
            lines.append(f'#define DM_GAME_{key.upper()} "{value}"')
        for key in ('save_throttle_offset', 'save_throttle_original', 'save_throttle_seconds',
                    'seed_offset', 'renderer_offset', 'render_scale_offset', 'render_resize_offset'):
            lines.append(f'#define DM_GAME_{key.upper()} {self.integer(key):#x}')
        return '\n'.join(lines) + '\n'


def load(name: str | None = None, *, root: Path = HERE) -> Game:
    selected = name or os.environ.get('DM_GAME') or DEFAULT_GAME
    if not re.fullmatch(r'[0-9A-F]{16}', selected):
        raise ValueError(f'invalid game Title ID: {selected!r}')
    directory = root / selected
    fields: dict[str, str] = {}
    builds: dict[str, str] = {}
    for line in (directory / 'game.tsv').read_text(encoding='utf-8').splitlines():
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        key, *values = line.split('\t')
        if key == 'build':
            if builds or len(values) < 3:
                raise ValueError('this first profile stage supports one build row per game')
            for pair in values[2:]:
                module, build = pair.split('=', 1)
                if module in builds or not re.fullmatch(r'[0-9a-f]{32}', build):
                    raise ValueError(f'invalid or duplicate module build: {pair}')
                builds[module] = build
        else:
            if len(values) != 1 or not values[0] or key in fields:
                raise ValueError(f'invalid or duplicate profile field: {key}')
            fields[key] = values[0]
    game = Game(directory, fields, builds)
    if game.get('title_id') != selected or not re.fullmatch(r'[0-9A-F]{16}', game.get('program_id')):
        raise ValueError('profile identity mismatch')
    if set(game.words('modules')) != set(builds):
        raise ValueError('profile module list and builds disagree')
    return game


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('list', 'get', 'header', 'digest'))
    parser.add_argument('--game', default=None)
    parser.add_argument('--key')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'list':
            for path in sorted(HERE.glob('*/game.tsv')):
                game = load(path.parent.name)
                print(f'{game.id}\t{game.get("name")}')
            return 0
        game = load(args.game)
        if args.command == 'get':
            if args.key is None:
                parser.error('get requires --key')
            print(game.get(args.key))
        elif args.command == 'digest':
            print(game.rewrite_digest())
        else:
            if args.output is None:
                parser.error('header requires --output')
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(game.header(), encoding='utf-8')
    except (OSError, ValueError, KeyError) as error:
        parser.exit(2, f'game profile: {error}\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
