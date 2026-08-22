# tools/

Host-side (Python) CLI tools supporting ArchiLudo's asset pipeline --
none of these run on RISC OS, they prepare things for it.

- `riscos_sprite.py` -- PNG <-> RISC OS old-style Sprite file converter.
  See [`../docs/GRAPHICS_TOOLING.md`](../docs/GRAPHICS_TOOLING.md) for the
  full manual (format details, usage, how it was verified). Needs
  Python 3 + Pillow (`pip install Pillow`).
