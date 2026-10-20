#!/usr/bin/env python3
"""Focused production packaging regression: assets/mapgoal is build-owned and
must be cleared on absent/reduced inputs, without touching unrelated assets."""
import importlib.util
import tempfile
from pathlib import Path

spec = importlib.util.spec_from_file_location("android_build", Path(__file__).resolve().parents[1] / "tools" / "build.py")
assert spec is not None and spec.loader is not None
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)

with tempfile.TemporaryDirectory() as td:
    root = Path(td); assets = root / "assets"; assets.mkdir()
    (assets / "keep.txt").write_text("unrelated")
    (assets / "mapgoal").mkdir()
    (assets / "mapgoal" / "stale.png").write_text("old")
    full = root / "full"; full.mkdir()
    for name in ("label_1.png", "label_1_old.png", "label_1_en.png", "sym_0.png", "cross.png", "mapgoal.json"):
        (full / name).write_text(name)
    assert build.stage_mapgoal_art(full, assets) == 6
    assert not (assets / "mapgoal" / "stale.png").exists()
    assert (assets / "keep.txt").read_text() == "unrelated"
    assert build.stage_mapgoal_art(None, assets) == 0
    assert not (assets / "mapgoal").exists()
    assert (assets / "keep.txt").read_text() == "unrelated"
    reduced = root / "reduced"; reduced.mkdir()
    (reduced / "label_1.png").write_text("new")
    assert build.stage_mapgoal_art(reduced, assets) == 1
    names = sorted(p.name for p in (assets / "mapgoal").iterdir())
    assert names == ["label_1.png"], names
    assert (assets / "keep.txt").read_text() == "unrelated"
print("PASS: mapgoal staging clears absent/reduced owned assets and preserves unrelated files")
