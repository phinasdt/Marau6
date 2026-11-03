"""Paint the impact textures used by AMarau6ShotFX.

Each texture is a 2x2 atlas of variations, picked at random per impact:
T_BloodSplatter (decal: splats with satellite drops and streaks, darker where the
pool is thick, a wet darker rim; coverage in alpha), T_BloodMist (sprite: puffs
of spray with flecks), T_BulletHole and its normal map T_BulletHole_N (decal:
punctured centre, crushed crater, chipped rim, cracks and a soot halo) and
T_ImpactDust (sprite: a burst of dust with grit, lit from above). Tile borders
are clear (and flat in the normal map) so neither the blend nor mips bleed.
Seeded, so the output is reproducible.
Usage: python -I scripts/Generate-ImpactTextures.py OUT_DIR
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter, zoom

TILE = 512
S = (TILE, TILE)
rng = np.random.default_rng(67)


def fbm(octaves=5, base=4, stretch=(1.0, 1.0)):
    """Value-noise fractal in [0, 1]; stretch > 1 elongates features along that axis."""
    out = np.zeros(S)
    amp, total = 1.0, 0.0
    for octave in range(octaves):
        cells = base * 2 ** octave
        grid = rng.random((max(2, int(cells / stretch[0])) + 3, max(2, int(cells / stretch[1])) + 3))
        layer = zoom(grid, (TILE / (grid.shape[0] - 3), TILE / (grid.shape[1] - 3)), order=3)
        out += amp * layer[:TILE, :TILE]
        total += amp
        amp *= 0.5
    out /= total
    return (out - out.min()) / (np.ptp(out) + 1e-9)


def coords():
    y, x = np.mgrid[-1:1:TILE * 1j, -1:1:TILE * 1j]
    return x, y


def smoothstep(edge0, edge1, value):
    t = np.clip((value - edge0) / (edge1 - edge0), 0, 1)
    return t * t * (3 - 2 * t)


def angular_noise(theta, harmonics=7, roughness=0.6):
    """Periodic noise around a circle in roughly [-1, 1]."""
    out = np.zeros_like(theta)
    amp = 1.0
    for k in range(1, harmonics + 1):
        out += amp * np.sin(k * theta + rng.uniform(0, 2 * np.pi)) * rng.uniform(0.5, 1.0)
        amp *= roughness
    return out / 2.0


def border(x, y, reach=0.94):
    return np.clip((reach - np.hypot(x, y)) * 12, 0, 1)


def ellipse(x, y, cx, cy, angle, length, width):
    """Gaussian ellipse field, long axis along angle."""
    dx, dy = x - cx, y - cy
    along = dx * np.cos(angle) + dy * np.sin(angle)
    across = -dx * np.sin(angle) + dy * np.cos(angle)
    return np.exp(-(along / length) ** 2 - (across / width) ** 2)


def blood_splatter_tile():
    x, y = coords()
    # Warping the coordinates turns round metaballs into ragged, organic edges.
    xw = x + (fbm(base=4, octaves=5) - 0.5) * 0.26
    yw = y + (fbm(base=4, octaves=5) - 0.5) * 0.26
    heading = rng.uniform(0, 2 * np.pi)
    hx, hy = np.cos(heading), np.sin(heading)
    field = np.zeros(S)
    # The main splat sits a little behind the spray direction.
    for _ in range(int(rng.integers(4, 8))):
        cx, cy = rng.normal(0, 0.1, 2) - np.array([hx, hy]) * 0.12
        radius = rng.uniform(0.13, 0.24)
        field += np.exp(-((xw - cx) ** 2 + (yw - cy) ** 2) / radius ** 2)
    # Fingers run out of the splat, mostly along the heading, each ending in a bead.
    for _ in range(int(rng.integers(6, 12))):
        angle = heading + rng.normal(0, 0.9)
        reach = rng.uniform(0.22, 0.5)
        width = rng.uniform(0.025, 0.05)
        along = xw * np.cos(angle) + yw * np.sin(angle)
        across = -xw * np.sin(angle) + yw * np.cos(angle)
        taper = width * np.clip(1 - 0.6 * along / reach, 0.3, 1)
        finger = np.exp(-(across / taper) ** 2) * smoothstep(0.0, 0.08, along) * smoothstep(reach, reach - 0.06, along)
        field += finger * 0.9
        field += 1.2 * np.exp(-((xw - reach * np.cos(angle)) ** 2 + (yw - reach * np.sin(angle)) ** 2) / (width * 1.4) ** 2)
    # Satellite drops thrown along the heading, smaller and longer the further they fly.
    for _ in range(int(rng.integers(30, 55))):
        angle = heading + rng.normal(0, 0.75)
        distance = rng.uniform(0.32, 0.86)
        radius = rng.uniform(0.008, 0.032) * (1.25 - distance)
        stretch = 1 + rng.uniform(0, 2.8) * distance
        cx, cy = distance * np.cos(angle), distance * np.sin(angle)
        field += 1.6 * ellipse(x, y, cx, cy, angle, radius * stretch, radius)
        if rng.random() < 0.3:
            # A thin tail back toward the source, the drop's flight path.
            tail = radius * stretch * rng.uniform(2, 4)
            field += 0.9 * ellipse(x, y, cx - np.cos(angle) * tail, cy - np.sin(angle) * tail, angle, tail, radius * 0.35)
    # A fine mist of specks over the whole tile.
    for _ in range(int(rng.integers(40, 80))):
        angle = heading + rng.normal(0, 1.3)
        distance = rng.uniform(0.15, 0.9)
        radius = rng.uniform(0.003, 0.009)
        field += 1.4 * ellipse(x, y, distance * np.cos(angle), distance * np.sin(angle), angle, radius * 1.5, radius)
    field = gaussian_filter(field, 0.7)
    coverage = smoothstep(0.42, 0.55, field)
    thickness = 1 - np.exp(-np.clip(field - 0.5, 0, None) * 1.4)
    # Blood dries darker at its edge: a rim just inside the outline.
    rim = coverage * (1 - smoothstep(0.5, 0.85, field))
    grain = fbm(base=8, octaves=4)
    thin = np.array([0.50, 0.035, 0.03])
    thick = np.array([0.20, 0.008, 0.012])
    rgb = thin + (thick - thin) * thickness[..., None]
    rgb *= (1 - 0.35 * rim)[..., None]
    rgb *= (0.85 + 0.3 * grain)[..., None]
    alpha = coverage * (0.82 + 0.18 * thickness) * border(x, y)
    return np.dstack([np.clip(rgb, 0, 1), alpha])


def blood_mist_tile():
    x, y = coords()
    heading = rng.uniform(0, 2 * np.pi)
    hx, hy = np.cos(heading), np.sin(heading)
    density = np.zeros(S)
    for _ in range(int(rng.integers(6, 11))):
        spread = rng.uniform(0, 0.4)
        cx, cy = np.array([hx, hy]) * spread + rng.normal(0, 0.14, 2)
        radius = rng.uniform(0.16, 0.34) * (1 - 0.4 * spread)
        density += np.exp(-((x - cx) ** 2 + (y - cy) ** 2) / radius ** 2)
    density = np.clip(density / density.max(), 0, 1)
    detail = fbm(base=4, octaves=6)
    wisps = 1 - np.abs(2 * fbm(base=5, octaves=5) - 1)
    density = np.clip(density * (0.3 + 0.7 * detail + 0.35 * wisps) - 0.2, 0, 1)
    density = gaussian_filter(density, 2.0)
    density /= max(density.max(), 1e-6)
    # Flecks: droplets flung out ahead of the cloud.
    flecks = np.zeros(S)
    for _ in range(int(rng.integers(50, 90))):
        angle = heading + rng.normal(0, 0.8)
        distance = rng.uniform(0.1, 0.85)
        radius = rng.uniform(0.004, 0.014)
        flecks = np.maximum(flecks, ellipse(x, y, distance * np.cos(angle), distance * np.sin(angle), angle,
                                            radius * rng.uniform(1, 2.5), radius) * rng.uniform(0.6, 1))
    alpha = np.clip(1.25 * density ** 1.1 + flecks, 0, 1) * border(x, y)
    # Lit from above, darker where dense.
    shade = np.clip(0.95 - 0.18 * y - 0.25 * density + 0.15 * (detail - 0.5), 0.4, 1)
    rgb = np.stack([0.62 * shade, 0.05 * shade, 0.045 * shade], -1)
    rgb[flecks > 0.3] *= 0.6
    return np.dstack([rgb, alpha])


def crack(x, y, start_radius, angle, length):
    """A jagged dark line out from the crater, rasterised by distance to its segments."""
    points = [np.array([np.cos(angle), np.sin(angle)]) * start_radius]
    segments = 7
    for index in range(segments):
        angle += rng.normal(0, 0.35)
        points.append(points[-1] + np.array([np.cos(angle), np.sin(angle)]) * length / segments)
        if rng.random() < 0.15 and index < segments - 2:
            # A short branch.
            branch_angle = angle + rng.choice([-1, 1]) * rng.uniform(0.5, 1.0)
            end = points[-1] + np.array([np.cos(branch_angle), np.sin(branch_angle)]) * length * rng.uniform(0.15, 0.3)
            yield points[-1], end, 0.004
    for index in range(segments):
        width = 0.007 * (1 - index / segments) + 0.002
        yield points[index], points[index + 1], width


def segment_distance(x, y, a, b):
    ab = b - a
    t = np.clip(((x - a[0]) * ab[0] + (y - a[1]) * ab[1]) / max(ab @ ab, 1e-9), 0, 1)
    return np.hypot(x - (a[0] + t * ab[0]), y - (a[1] + t * ab[1]))


def bullet_hole_tile():
    # Painted at twice the scale so the mark fills the decal; the tile edge is r = 0.5.
    x_tile, y_tile = coords()
    x, y = x_tile * 0.5, y_tile * 0.5
    r = np.hypot(x, y)
    theta = np.arctan2(y, x)
    hole_radius = rng.uniform(0.09, 0.11) * (1 + 0.22 * angular_noise(theta, 9, 0.7))
    rim_radius = rng.uniform(0.23, 0.29) * (1 + 0.28 * angular_noise(theta, 11, 0.75))
    # Chips knocked out of the rim widen the crater in places.
    chips = np.zeros(S)
    for _ in range(int(rng.integers(3, 7))):
        angle = rng.uniform(0, 2 * np.pi)
        distance = rng.uniform(0.18, 0.27)
        size = rng.uniform(0.03, 0.06)
        chips = np.maximum(chips, ellipse(x, y, distance * np.cos(angle), distance * np.sin(angle), angle + rng.normal(0, 0.5),
                                          size * rng.uniform(1, 1.8), size))
    hole = smoothstep(hole_radius + 0.012, hole_radius - 0.012, r)
    crater = np.maximum(smoothstep(rim_radius + 0.012, rim_radius - 0.012, r), smoothstep(0.45, 0.6, chips))
    crater = np.maximum(crater, hole)
    lines = np.zeros(S)
    for _ in range(int(rng.integers(5, 10))):
        angle = rng.uniform(0, 2 * np.pi)
        for a, b, width in crack(x, y, 0.15, angle, rng.uniform(0.12, 0.3)):
            lines = np.maximum(lines, smoothstep(width, width * 0.3, segment_distance(x, y, a, b)))
    lines *= 1 - crater
    grit = fbm(base=16, octaves=4)
    soot_noise = fbm(base=4, octaves=5)
    soot = np.exp(-(r / rng.uniform(0.25, 0.31)) ** 2) * (0.35 + 0.65 * soot_noise) * 0.7
    # Colour: soot-black puncture, a lighter crushed crater darkening toward the hole,
    # dark cracks, a sooty halo.
    depth = smoothstep(rim_radius, hole_radius, r)
    crushed = (0.42 - 0.25 * depth) * (0.75 + 0.5 * grit)
    tone = np.full(S, 0.07)
    tone = tone * (1 - crater) + crushed * crater
    tone = tone * (1 - hole) + 0.015 * hole
    tone = tone * (1 - lines) + 0.05 * lines
    rgb = np.stack([tone * 1.02, tone, tone * 0.96], -1)
    alpha = np.maximum.reduce([hole, crater * 0.95, lines * 0.85, soot]) * border(x_tile, y_tile)
    # Height: a crater sloping down into the puncture, a slightly raised ragged rim,
    # grooves along the cracks.
    height = -hole * 1.0 - crater * (0.15 + 0.45 * depth) + 0.12 * smoothstep(0.1, 0.0, np.abs(r - rim_radius)) * (1 - crater * 0.5)
    height += crater * (grit - 0.5) * 0.25 - lines * 0.2
    height = gaussian_filter(height * border(x_tile, y_tile), 1.5)
    dv, du = np.gradient(height)
    strength = 30.0
    normal = np.dstack([-du * strength, -dv * strength, np.ones(S)])
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    return np.dstack([rgb, alpha]), normal * 0.5 + 0.5


def dust_tile():
    x, y = coords()
    density = np.zeros(S)
    for _ in range(int(rng.integers(6, 10))):
        cx, cy = rng.normal(0, 0.2, 2)
        radius = rng.uniform(0.2, 0.4)
        density += np.exp(-((x - cx) ** 2 + (y - cy) ** 2) / radius ** 2)
    density = np.clip(density / density.max(), 0, 1)
    detail = fbm(base=4, octaves=6)
    billows = 1 - np.abs(2 * fbm(base=6, octaves=5) - 1)
    density = np.clip(density * (0.3 + 0.75 * detail + 0.3 * billows) - 0.15, 0, 1)
    density *= np.clip((1 - np.hypot(x, y)) * 4, 0, 1)
    density = gaussian_filter(density, 1.5)
    density /= max(density.max(), 1e-6)
    # Grit: dark chips and streaks thrown out of the burst.
    grit = np.zeros(S)
    for _ in range(int(rng.integers(40, 80))):
        angle = rng.uniform(0, 2 * np.pi)
        distance = rng.uniform(0.05, 0.8)
        radius = rng.uniform(0.004, 0.013)
        grit = np.maximum(grit, ellipse(x, y, distance * np.cos(angle), distance * np.sin(angle), angle,
                                        radius * rng.uniform(1, 3), radius) * rng.uniform(0.6, 1))
    alpha = np.clip(density ** 1.2 + grit * 0.9, 0, 1) * border(x, y)
    shade = np.clip(0.9 - 0.25 * y + 0.18 * (detail - 0.5) - 0.12 * density, 0.35, 1)
    shade = shade * (1 - 0.7 * grit)
    rgb = np.stack([shade, shade * 0.95, shade * 0.88], -1)
    return np.dstack([rgb, alpha])


def atlas(tiles):
    return np.vstack([np.hstack(tiles[0:2]), np.hstack(tiles[2:4])])


def save(image, path):
    mode = "RGBA" if image.shape[-1] == 4 else "RGB"
    Image.fromarray((np.clip(image, 0, 1) * 255 + 0.5).astype(np.uint8), mode).save(path)
    print("wrote", path)


out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
save(atlas([blood_splatter_tile() for _ in range(4)]), out / "T_BloodSplatter.png")
save(atlas([blood_mist_tile() for _ in range(4)]), out / "T_BloodMist.png")
holes = [bullet_hole_tile() for _ in range(4)]
save(atlas([colour for colour, _ in holes]), out / "T_BulletHole.png")
save(atlas([normal for _, normal in holes]), out / "T_BulletHole_N.png")
save(atlas([dust_tile() for _ in range(4)]), out / "T_ImpactDust.png")
