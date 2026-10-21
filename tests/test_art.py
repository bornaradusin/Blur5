import subprocess, sys, pathlib
from PIL import Image
ROOT = pathlib.Path(__file__).resolve().parents[1]

# Sizes and modes the app build checks (build_game_app.sh): icon 512x512, background
# 3840x2160 RGB; and the page's own background, 1024x576 RGB (the app reads pictures of at
# most 1024x1024 and 2 MB).
FILES = (("blur5.png", (512, 512), "RGB"),
         ("blur5-bg.png", (3840, 2160), "RGB"),
         ("blur5-page.png", (1024, 576), "RGB"))


def test_art_sizes_and_determinism(tmp_path):
    for run in (tmp_path / "a", tmp_path / "b"):
        subprocess.run([sys.executable, str(ROOT / "art/make-art.py"), str(run)], check=True)
    for name, size, mode in FILES:
        im = Image.open(tmp_path / "a" / name)
        assert im.size == size and im.mode == mode
        assert (tmp_path / "a" / name).read_bytes() == (tmp_path / "b" / name).read_bytes()


def test_committed_art_matches_the_sources(tmp_path):
    subprocess.run([sys.executable, str(ROOT / "art/make-art.py"), str(tmp_path)], check=True)
    for name, _, _ in FILES:
        assert (tmp_path / name).read_bytes() == (ROOT / "art" / name).read_bytes(), name
