# tools/

Host-side (Python) CLI tools supporting ArchiLudo's asset pipeline --
none of these run on RISC OS, they prepare things for it.

- `riscos_sprite.py` -- PNG <-> RISC OS old-style Sprite file converter.
  See [`../docs/GRAPHICS_TOOLING.md`](../docs/GRAPHICS_TOOLING.md) for the
  full manual (format details, usage, how it was verified). Needs
  Python 3 + Pillow (`pip install Pillow`).
- `test_riscos_sprite.py` -- self-contained regression tests for the
  above (`python3 tools/test_riscos_sprite.py`, no external files
  needed). Extend this when a new format edge case is found rather than
  only checking it by hand -- see `docs/GRAPHICS_TOOLING.md`'s "Round
  7.16" for what it currently locks in.
