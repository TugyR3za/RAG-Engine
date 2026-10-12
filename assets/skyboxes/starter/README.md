# Starter sky cubemap

Original offline procedural artwork generated for RAG Engine by
`tools/generate_starter_sky.py`. No downloaded imagery, external assets, or online
service was used. The six PNG images are dedicated to the public domain under
[CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/).

Each image is 256x256, RGB, sRGB encoded, top row first. Runtime decoding expands
to RGBA8. Face order is `px.png`, `nx.png`, `py.png`, `ny.png`, `pz.png`, `nz.png`
(+X, -X, +Y, -Y, +Z, -Z). Do not flip or rotate them on import.

The generator samples one continuous world-direction function at texel centers.
The horizon is at world Y=0; a warm glow points toward +X/-Z. Both hemispheres are
defined. No face-local decorations or random patterns introduce edge mismatches.

Regenerate intentionally with `python tools/generate_starter_sky.py`, or verify
without writing with `python tools/generate_starter_sky.py --check`. Neither the
build nor the engine runs the generator. See [skybox documentation](../../../docs/skybox.md)
for exact orientations, scene fields, resource lifetime, and testing.
