# Space sky in the test level

`Content/FirstPerson/Lvl_FirstPerson.umap` contains a non-colliding, two-sided sky sphere named `Marau6 Space Sky`. Its collision profile must be `NoCollision` (setting only Collision Enabled does not survive a reload) and Can Ever Affect Navigation is off. If the sphere keeps collision, it fills the whole arena: the runtime navmesh ends up empty, the destruction sections cannot spawn and the round never becomes ready ("Backend nao esta pronto").

The sphere's unlit material, `M_Marau6_SpaceSky`, is procedural: a Custom node evaluates `scripts/space_sky.hlsl` per pixel from the view direction, so the sky stays sharp at any resolution or FOV and keeps moving. It draws:

- Four aurora curtains (green to violet, cyan to blue, gold to orange, pink to purple) with drifting vertical rays, slow waves and pulses. Each curtain is a thin vertical sheet; the shader finds where the view ray crosses it, so the folds stay crisp, and switches to a soft glow where a curtain is seen edge-on.
- Slowly churning nebulae (teal, violet, magenta and amber gas with filaments and dark dust lanes) and a faint milky-way band.
- Three layers of stars with colour temperature and twinkle; the bright layer has small halos. Star cores are never narrower than a pixel, so they do not shimmer.

The material's Is Sky flag must stay off: with `r.SkyAtmosphere=0` a sky-flagged mesh is not drawn and the sky stays black. Scalar parameters `Speed`, `AuroraGain`, `NebulaGain` and `StarGain` (all 1 by default) can be tuned on a material instance. The template's VolumetricCloud actor was removed from the map; it drew a cloud layer over the sky.

The ochre and blue planets are separate transparent, unlit planes inside the sky sphere. They face the arena, so their silhouettes stay round instead of stretching across the sky sphere's UVs. Their source images are 1254×1254.

The sky material and both planet materials offset their vertices by the camera position (World Position Offset), so the sphere and planets stay centred on the viewer. They read as infinitely far away and do not drift against the stars while the player walks. Making the sphere much larger instead does not work: at around 20 km radius Unreal stops drawing it. The planet meshes use a bounds scale of 2, because culling still uses their unshifted position.

The existing daylight atmosphere and height fog actors remain in the map. `[SystemSettings]` in `Config/DefaultEngine.ini` disables both render effects so they do not cover the space sky. These two settings currently apply to all project maps. Restart the editor to load the settings and the updated map, then use the normal two-client PIE flow in `README.md` to inspect the sky while moving around the arena. After the material changes, the first launch compiles its shaders; the sky stays black for roughly half a minute until they finish.

To change the sky, edit `scripts/space_sky.hlsl` and run `scripts/setup_space_sky.py` in Unreal's Python Script commandlet while the editor is closed. The shader file must stay within the HLSL subset that also compiles as GLSL (see its header comment). `scripts/verify_space_sky.py` checks the procedural material, the missing clouds, saved actors and materials, collision, navigation and render settings.
