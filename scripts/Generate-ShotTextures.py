"""Paint the muzzle flash and muzzle smoke textures used by AMarau6ShotFX.

Each texture is a 2x2 atlas of variations, picked at random per shot:
T_MuzzleFlash_Side (the flame seen from the side, muzzle on the left edge),
T_MuzzleFlash_Front (the star seen down the barrel) and T_MuzzleSmoke (soft
puffs, lit from above, shape in alpha). Flash tiles are premultiplied for an
additive blend. Seeded, so the output is reproducible.
Usage: python -I scripts/Generate-ShotTextures.py OUT_DIR
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter, zoom

TILE = 256
rng = np.random.default_rng(66)


def fbm(shape, octaves=5, base=4, stretch=(1.0, 1.0)):
    """Value-noise fractal in [0, 1]; stretch > 1 elongates features along that axis."""
    out = np.zeros(shape)
    amp, total = 1.0, 0.0
    for octave in range(octaves):
        cells = base * 2 ** octave
        grid = rng.random((max(2, int(cells / stretch[0])) + 3, max(2, int(cells / stretch[1])) + 3))
        layer = zoom(grid, (shape[0] / (grid.shape[0] - 3), shape[1] / (grid.shape[1] - 3)), order=3)
        out += amp * layer[: shape[0], : shape[1]]
        total += amp
        amp *= 0.5
    out /= total
    return (out - out.min()) / (np.ptp(out) + 1e-9)


def fire(heat):
    """White-hot core through yellow and orange to a dim red rim, premultiplied."""
    heat = np.clip(heat, 0, 1)
    r = np.clip(heat * 2.2, 0, 1)
    g = np.clip(heat * 1.9 - 0.35, 0, 1) ** 1.2
    b = np.clip(heat * 2.4 - 1.55, 0, 1)
    return np.stack([r, g, b], -1) * heat[..., None] ** 0.6


def side_tile():
    # u along the barrel (0 = muzzle), v across it.
    v, u = np.mgrid[-1:1:TILE * 1j, 0:1:TILE * 1j]
    length = rng.uniform(0.75, 0.95)
    x = u / length
    # The plume swells out of the muzzle, pinches, then frays into tongues.
    width = 0.10 + 0.55 * np.sin(np.clip(x, 0, 1) * np.pi) ** 0.8 * (1 - 0.35 * x)
    # Ridged noise stretched along the barrel gives streaky licks of flame.
    turb = 1 - np.abs(2 * fbm((TILE, TILE), base=3, stretch=(0.3, 3.0)) - 1)
    bend = (fbm((TILE, TILE), octaves=3, base=2) - 0.5) * 0.35 * x
    body = np.exp(-(((v + bend) / width) ** 2) * 2.2)
    body *= np.clip(1 - x, 0, 1) ** 0.9 * turb ** 1.6
    # Two or three thin jets break out of the plume at slight angles.
    for _ in range(rng.integers(2, 4)):
        angle = rng.uniform(-0.45, 0.45)
        reach = rng.uniform(0.5, 1.0) * length
        jet = np.exp(-((v - angle * u) / (0.035 + 0.06 * u)) ** 2) * np.clip(1 - u / reach, 0, 1) ** 1.5
        body = np.maximum(body, jet * rng.uniform(0.5, 0.85))
    core = np.exp(-(u / 0.07) ** 2 - (v / 0.09) ** 2)
    heat = np.clip(body * 0.85 + core, 0, 1)
    heat = gaussian_filter(heat, 0.8)
    # Fade the tile borders to black so neither the blend nor mips bleed between tiles.
    heat *= np.clip((1 - np.abs(v)) * 8, 0, 1) * np.clip((1 - u) * 10, 0, 1) * np.clip(u * 40 + 0.3, 0, 1)
    return np.dstack([fire(heat), heat])


def front_tile():
    y, x = np.mgrid[-1:1:TILE * 1j, -1:1:TILE * 1j]
    r = np.hypot(x, y)
    theta = np.arctan2(y, x)
    petals = int(rng.integers(5, 8))
    offset = rng.uniform(0, 2 * np.pi)
    # Sharp, uneven star arms: each arm gets its own reach.
    reach = rng.uniform(0.55, 0.95, petals)
    arm_index = np.round((theta - offset) / (2 * np.pi / petals)) % petals
    arm_angle = (theta - offset) - np.round((theta - offset) / (2 * np.pi / petals)) * (2 * np.pi / petals)
    arm_reach = reach[arm_index.astype(int)]
    arms = np.exp(-(arm_angle / (0.1 + 0.4 * np.clip(1 - r / arm_reach, 0, 1))) ** 2)
    arms *= np.clip(1 - r / arm_reach, 0, 1) ** 1.1
    turb = fbm((TILE, TILE), base=4)
    licks = 1 - np.abs(2 * fbm((TILE, TILE), base=5) - 1)
    disc = np.exp(-(r / 0.38) ** 2) * (0.35 + 0.6 * licks)
    core = np.exp(-(r / 0.12) ** 2)
    heat = np.clip(np.maximum(disc, arms * (0.45 + 0.6 * turb)) * 0.9 + core, 0, 1)
    heat = gaussian_filter(heat, 0.8) * np.clip((1 - r) * 6, 0, 1)
    return np.dstack([fire(heat), heat])


def smoke_tile():
    y, x = np.mgrid[-1:1:TILE * 1j, -1:1:TILE * 1j]
    # A cluster of overlapping blobs reads as billowing rather than a disc.
    density = np.zeros((TILE, TILE))
    for _ in range(int(rng.integers(5, 9))):
        cx, cy = rng.normal(0, 0.22, 2)
        radius = rng.uniform(0.25, 0.45)
        density += np.exp(-((x - cx) ** 2 + (y - cy) ** 2) / radius ** 2)
    density = np.clip(density / density.max(), 0, 1)
    detail = fbm((TILE, TILE), base=4, octaves=6)
    density = np.clip(density * (0.35 + 0.9 * detail) - 0.12, 0, 1)
    density *= np.clip((1 - np.hypot(x, y)) * 4, 0, 1)
    density = gaussian_filter(density, 1.2)
    alpha = np.clip(density / max(density.max(), 1e-6), 0, 1) ** 1.3
    # Lit from above: lighter on top, a darker underside, a little noise in the tone.
    shade = np.clip(0.72 - 0.22 * y + 0.12 * (detail - 0.5), 0, 1)
    return np.dstack([shade, shade, shade * 0.98, alpha])


def atlas(painter):
    tiles = [painter() for _ in range(4)]
    return np.vstack([np.hstack(tiles[0:2]), np.hstack(tiles[2:4])])


def save(image, path):
    Image.fromarray((np.clip(image, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA").save(path)
    print("wrote", path)


out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
save(atlas(side_tile), out / "T_MuzzleFlash_Side.png")
save(atlas(front_tile), out / "T_MuzzleFlash_Front.png")
save(atlas(smoke_tile), out / "T_MuzzleSmoke.png")
