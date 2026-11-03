// Procedural space sky for M_Marau6_SpaceSky: aurora curtains, drifting nebulae and
// twinkling stars, evaluated per pixel from the view direction (Z up), so it stays sharp
// at any resolution or FOV. scripts/setup_space_sky.py wraps these functions in a struct
// inside the material's Custom node and calls SpaceSky. Keep to the HLSL subset that also
// compiles as GLSL with a few #defines: no arrays, statics, implicit scalar-to-vector
// casts, or names reserved in either language (sample, line, half, smooth, filter...).

float Hash13(float3 p3)
{
    p3 = frac(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return frac((p3.x + p3.y) * p3.z);
}

float3 Hash33(float3 p3)
{
    p3 = frac(p3 * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yxz + 33.33);
    return frac((p3.xxy + p3.yxx) * p3.zyx);
}

float Noise1(float x)
{
    float i = floor(x);
    float f = frac(x);
    float u = f * f * (3.0 - 2.0 * f);
    return lerp(Hash13(float3(i, 1.7, 3.1)), Hash13(float3(i + 1.0, 1.7, 3.1)), u);
}

float Noise3(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    float3 u = f * f * (3.0 - 2.0 * f);
    float n000 = Hash13(i);
    float n100 = Hash13(i + float3(1.0, 0.0, 0.0));
    float n010 = Hash13(i + float3(0.0, 1.0, 0.0));
    float n110 = Hash13(i + float3(1.0, 1.0, 0.0));
    float n001 = Hash13(i + float3(0.0, 0.0, 1.0));
    float n101 = Hash13(i + float3(1.0, 0.0, 1.0));
    float n011 = Hash13(i + float3(0.0, 1.0, 1.0));
    float n111 = Hash13(i + float3(1.0, 1.0, 1.0));
    return lerp(lerp(lerp(n000, n100, u.x), lerp(n010, n110, u.x), u.y),
                lerp(lerp(n001, n101, u.x), lerp(n011, n111, u.x), u.y), u.z);
}

// Rotating each octave hides the lattice axes of the value noise.
float3 OctaveRotate(float3 p)
{
    return float3(dot(p, float3(0.00, 1.60, 1.20)),
                  dot(p, float3(-1.60, 0.72, -0.96)),
                  dot(p, float3(-1.20, -0.96, 1.28)));
}

float Fbm(float3 p, int octaves)
{
    float sum = 0.0;
    float amp = 0.5;
    for (int i = 0; i < octaves; i++)
    {
        sum += amp * Noise3(p);
        p = OctaveRotate(p);
        amp *= 0.5;
    }
    return sum / (1.0 - 2.0 * amp);
}

// Sharp creases for the nebula filaments.
float Ridged(float3 p, int octaves)
{
    float sum = 0.0;
    float amp = 0.5;
    for (int i = 0; i < octaves; i++)
    {
        float n = 1.0 - abs(2.0 * Noise3(p) - 1.0);
        sum += amp * n * n;
        p = OctaveRotate(p);
        amp *= 0.5;
    }
    return sum / (1.0 - 2.0 * amp);
}

// Cube-face coordinates (xy in -1..1, z = face 0..5). The atan warp keeps the cells
// close to equal-sized, so the stars do not bunch up towards the face corners.
float3 CubeCoord(float3 d)
{
    float3 a = abs(d);
    float face;
    float2 uv;
    if (a.x >= a.y && a.x >= a.z)
    {
        face = d.x > 0.0 ? 0.0 : 1.0;
        uv = d.yz / a.x;
    }
    else if (a.y >= a.z)
    {
        face = d.y > 0.0 ? 2.0 : 3.0;
        uv = d.xz / a.y;
    }
    else
    {
        face = d.z > 0.0 ? 4.0 : 5.0;
        uv = d.xy / a.z;
    }
    return float3(atan(uv) * 1.2732395, face);
}

float3 StarColor(float t)
{
    float3 c = lerp(float3(0.62, 0.74, 1.0), float3(1.0, 0.97, 0.92), saturate(t * 2.0));
    return lerp(c, float3(1.0, 0.70, 0.45), saturate(t * 2.0 - 1.0));
}

// One jittered star per occupied cell. With reach 0 only the pixel's own cell is read,
// so the jitter keeps stars away from the cell edges; reach 1 reads the neighbours too,
// which lets the bright layer carry a halo. The core is never narrower than a pixel
// (pix is the pixel footprint in radians), so stars stay crisp without shimmering.
float3 StarLayer(float3 d, float cells, float occupancy, float sizeRad, float glow,
                 float seed, float time, float pix, int reach)
{
    float3 cube = CubeCoord(d);
    float2 g = (cube.xy * 0.5 + 0.5) * cells;
    float2 cell = floor(g);
    float cellRad = 1.5707963 / cells;
    float3 col = float3(0.0, 0.0, 0.0);
    for (int y = -reach; y <= reach; y++)
    {
        for (int x = -reach; x <= reach; x++)
        {
            float2 c = cell + float2(float(x), float(y));
            if (c.x < 0.0 || c.y < 0.0 || c.x >= cells || c.y >= cells)
                continue;
            float3 id = float3(c, cube.z * 17.0 + seed);
            float3 h = Hash33(id);
            if (h.z > occupancy)
                continue;
            float3 k = Hash33(id + 71.3);
            float2 pos = c + 0.25 + 0.5 * h.xy;
            float dist = length(g - pos) * cellRad;
            float mag = pow(k.x, 8.0);
            float sigma = max(pix * 0.6, sizeRad * (0.5 + mag));
            float core = exp(-0.5 * dist * dist / (sigma * sigma));
            float halo = glow * mag * mag * 0.25 * sigma * sigma / (dist * dist + sigma * sigma)
                         * smoothstep(cellRad, cellRad * 0.4, dist);
            float twinkle = 1.0 + 0.45 * (0.6 * sin(time * (1.3 + 4.0 * k.y) + 6.2831 * k.z)
                                        + 0.4 * sin(time * (3.1 + 6.0 * k.z) + 6.2831 * k.x));
            col += StarColor(Hash13(id + 19.7)) * (core * (0.02 + 1.6 * mag) + halo) * twinkle;
        }
    }
    return col;
}

float3 NebulaTint(float t)
{
    float3 teal = float3(0.04, 0.55, 0.55);
    float3 violet = float3(0.32, 0.10, 0.80);
    float3 magenta = float3(0.85, 0.10, 0.45);
    float3 amber = float3(0.95, 0.42, 0.08);
    t = saturate(t) * 3.0;
    if (t < 1.0)
        return lerp(teal, violet, t);
    if (t < 2.0)
        return lerp(violet, magenta, t - 1.0);
    return lerp(magenta, amber, t - 2.0);
}

// Domain-warped clouds of glowing gas with ridged filaments, slowly churning.
// body and dust are shared with the milky way glow and the star extinction.
float3 Nebula(float3 d, float time, out float body, out float dust)
{
    float3 p = d * 1.8;
    float3 drift = float3(time * 0.004, -time * 0.003, time * 0.002);
    float qx = Fbm(p + drift, 3);
    float qy = Fbm(p + float3(5.2, 1.3, 2.8) - drift, 3);
    float3 w = p * 2.0 + 3.0 * float3(qx, qy, qx - qy);
    body = Fbm(w + drift * 2.0, 5);
    float fil = Ridged(w * 2.3 - drift * 3.0, 3);
    float region = smoothstep(0.42, 0.68, Fbm(d * 0.9 + float3(11.0, 3.0, 7.0), 2));
    float gas = smoothstep(0.40, 0.85, body);
    float density = region * (0.75 * gas + 1.1 * pow(fil, 5.0) * smoothstep(0.30, 0.60, body));
    float3 tint = NebulaTint(Fbm(d * 0.7 + float3(-4.0, 8.0, 1.0), 2) * 1.8 - 0.4 + 0.5 * qx);
    float3 hot = lerp(tint, float3(1.0, 0.92, 0.85), 0.35 * gas * gas);
    dust = smoothstep(0.52, 0.74, Fbm(w * 1.4 + float3(3.0, 3.0, 3.0), 3)) * (0.25 + 0.6 * region);
    return hot * density;
}

void RibbonParams(int k, out float angle, out float shift, out float len, out float phase,
                  out float3 low, out float3 high)
{
    if (k == 0)
    {
        angle = 0.35; shift = 1.7; len = 6.0; phase = 0.0;
        low = float3(0.10, 1.00, 0.35); high = float3(0.55, 0.10, 0.85);
    }
    else if (k == 1)
    {
        angle = 2.00; shift = -2.4; len = 7.0; phase = 2.1;
        low = float3(0.05, 0.80, 1.00); high = float3(0.30, 0.15, 1.00);
    }
    else if (k == 2)
    {
        angle = -0.95; shift = 3.2; len = 8.0; phase = 4.2;
        low = float3(0.95, 0.95, 0.15); high = float3(1.00, 0.30, 0.05);
    }
    else
    {
        angle = 2.75; shift = -0.9; len = 5.0; phase = 5.7;
        low = float3(1.00, 0.20, 0.60); high = float3(0.45, 0.10, 0.95);
    }
}

// Signed horizontal distance from p to a curtain's wavy centre line, with the line's unit
// normal there (the direction the sheet faces) and the position along the curtain.
float CurtainDist(float2 p, float2 axis, float2 side, float shift, float phase, float time,
                  out float along, out float2 normal)
{
    along = dot(p, axis);
    float a0 = along * 0.45 + time * 0.07 + phase;
    float a1 = along * 1.3 - time * 0.13 + phase * 2.0;
    float a2 = along * 3.7 + time * 0.31;
    float curve = 0.55 * sin(a0) + 0.25 * sin(a1) + 0.12 * sin(a2);
    float slope = 0.2475 * cos(a0) + 0.325 * cos(a1) + 0.444 * cos(a2);
    normal = normalize(side - slope * axis);
    return dot(p, side) - shift - curve;
}

// Four curtains, each a thin vertical sheet hanging in a layer of the sky between heights
// 1 and 2.4. The ray's path through the layer is scanned for the exact point where it
// crosses each sheet, so the folds stay sharp at any resolution. The sheet glows in its
// own colour at the lower edge, fades into a second colour at the top, breaks into
// drifting vertical rays and brightens where the view grazes it. Seen edge-on, the
// crossings jump between scan steps, so there a soft volumetric glow takes over.
float3 Aurora(float3 d, float time, float pix)
{
    float3 acc = float3(0.0, 0.0, 0.0);
    if (d.z < 0.015)
        return acc;
    float2 base = d.xy / d.z;
    float planar = max(length(d.xy), 0.0001);
    float width = 0.13;
    float widthEff = max(width, 1.4 / 18.0 * planar / d.z * 0.7);
    for (int k = 0; k < 4; k++)
    {
        float angle;
        float shift;
        float len;
        float phase;
        float3 low;
        float3 high;
        RibbonParams(k, angle, shift, len, phase, low, high);
        float2 axis = float2(cos(angle), sin(angle));
        float2 side = float2(-axis.y, axis.x);
        float pulse = 0.55 + 0.45 * sin(time * 0.11 + phase * 1.7);
        float prevAlong;
        float2 prevNormal;
        float prevDist = CurtainDist(base, axis, side, shift, phase, time, prevAlong, prevNormal);
        float baseAlong = prevAlong;
        float nearest = abs(prevDist);
        float nearestT = 0.0;
        for (int i = 1; i <= 18; i++)
        {
            float t = float(i) / 18.0;
            float along;
            float2 normal;
            float dist = CurtainDist(base * (1.0 + 1.4 * t), axis, side, shift, phase, time,
                                     along, normal);
            if (abs(dist) < nearest)
            {
                nearest = abs(dist);
                nearestT = t;
            }
            float soft = exp(-dist * dist / (widthEff * widthEff)) * (width / widthEff);
            if (soft > 0.002)
            {
                acc += lerp(low, high, smoothstep(0.1, 0.7, t))
                     * (0.07 * soft * exp(-t * 3.0) * pulse
                        * smoothstep(len, len * 0.4, abs(along))
                        * (0.6 + 0.4 * sin(along * 1.7 - time * 0.9 + phase)));
            }
            if (dist * prevDist <= 0.0)
            {
                // One secant step on top of the linear guess keeps the crossing height
                // continuous between scan intervals.
                float f = saturate(abs(prevDist) / max(abs(prevDist - dist), 0.00001));
                float midAlong;
                float2 midNormal;
                float mid = CurtainDist(base * (1.0 + 1.4 * (float(i) - 1.0 + f) / 18.0),
                                        axis, side, shift, phase, time, midAlong, midNormal);
                if (mid * prevDist > 0.0)
                    f = f + (1.0 - f) * saturate(abs(mid) / max(abs(mid - dist), 0.00001));
                else
                    f = f * saturate(abs(prevDist) / max(abs(prevDist - mid), 0.00001));
                float tc = (float(i) - 1.0 + f) / 18.0;
                float ac = lerp(prevAlong, along, f);
                float h = 1.0 + 1.4 * tc;
                float footprint = pix * h / (d.z * d.z);
                float r0 = Noise1(ac * 31.0 + phase * 10.0 + time * 0.35);
                float r1 = Noise1(ac * 9.0 - time * 0.21 + phase);
                float rays = saturate(1.3 * r0 * r0 + 0.8 * r1 * r1 - 0.25);
                rays = lerp(0.35, rays, saturate(1.5 - footprint * 40.0));
                float profile = smoothstep(0.0, 0.05, tc) * exp(-tc * (4.0 - 2.8 * rays));
                float facing = abs(dot(d.xy, normal)) / planar;
                float graze = 0.08 / max(abs(dot(d.xy, normal)), 0.18)
                            * smoothstep(0.05, 0.35, facing);
                float wave = 0.6 + 0.4 * sin(ac * 1.7 - time * 0.9 + phase);
                float ends = smoothstep(len, len * 0.4, abs(ac));
                float edge = 1.0 + 1.5 * exp(-tc * 30.0);
                acc += lerp(low, high, smoothstep(0.1, 0.7, tc))
                     * (graze * profile * edge * (0.15 + 1.6 * rays) * wave * ends * pulse);
            }
            prevDist = dist;
            prevAlong = along;
        }
        float glow = exp(-nearest * nearest * 5.0) * exp(-nearestT * 2.5)
                   * smoothstep(len * 1.2, len * 0.3, abs(baseAlong));
        acc += lerp(low, high, 0.35) * (0.05 * glow * pulse);
    }
    return acc * smoothstep(0.015, 0.12, d.z);
}

float3 HorizonGlow(float3 d, float time)
{
    float h = exp(-abs(d.z - 0.01) * 18.0);
    float n = Fbm(d * 5.0 + float3(0.0, 0.0, time * 0.01), 4);
    float3 c = lerp(float3(0.45, 0.06, 0.20), float3(0.55, 0.18, 0.04), n);
    float patches = saturate((n - 0.35) * 2.5);
    return c * h * patches * patches * 0.15;
}

float3 SpaceSky(float3 d, float time, float pix, float auroraGain, float nebulaGain,
                float starGain)
{
    float body;
    float dust;
    float3 neb = Nebula(d, time, body, dust);
    float3 bandNormal = normalize(float3(0.35, -0.55, 0.76));
    float bd = dot(d, bandNormal);
    float band = exp(-bd * bd * 14.0);
    float density = 0.55 + 1.2 * band;

    float3 stars = StarLayer(d, 560.0, 0.10 * density, 0.00015, 0.0, 1.0, time, pix, 0) * 0.5
                 + StarLayer(d, 210.0, 0.14 * density, 0.00025, 0.0, 2.0, time, pix, 0) * 0.9
                 + StarLayer(d, 70.0, 0.10, 0.00045, 1.0, 3.0, time, pix, 1) * 1.6;
    float3 milkyWay = float3(0.30, 0.32, 0.42) * band * (0.15 + 0.6 * body) * 0.12;
    float3 col = float3(0.002, 0.003, 0.007) + milkyWay + stars * starGain;
    col = (col + neb * (0.8 * nebulaGain)) * exp(-dust * 2.5);
    col += HorizonGlow(d, time);

    col += Aurora(d, time, pix) * (1.3 * auroraGain);
    return col;
}
