"""Package the model and integration assets; include only current sprite exports."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

root = Path(__file__).resolve().parent
model = root / "tv_bot.glb"
files = [model, root / "model.json", root / "README.md", *sorted((root / "textures").glob("*.png"))]
for name in ("pico-preview.png", "pico-expressions.png"):
    path = root / name
    if path.exists() and path.stat().st_mtime >= model.stat().st_mtime:
        files.append(path)
with ZipFile(root / "desktop-pet.zip", "w", compression=ZIP_DEFLATED) as archive:
    for path in files:
        archive.write(path, path.relative_to(root))
print(f"Packaged {len(files)} assets -> {root / 'desktop-pet.zip'}")
