# Planet Atmosphere Plugin

Multi-planet GPU-driven volumetric atmosphere and weather system for Unreal Engine 5.6.
Custom renderer (no `USkyAtmosphereComponent`), any number of independent planets per level,
Earth-scale radii and large inter-planet distances.

## Structure

```
PlanetAtmosphere/
├── PlanetAtmosphere.uplugin                (single Runtime module, LoadingPhase PostConfigInit — required for global shaders)
├── Source/PlanetAtmosphere/
│   ├── Public/
│   │   ├── PlanetAtmosphereActor.h          thin placeable actor
│   │   ├── PlanetAtmosphereComponent.h      per-planet parameters (meters), bounds, scene proxy creation
│   │   ├── PlanetAtmosphereSceneProxy.h     render-side copy of the parameters
│   │   ├── AtmosphereProxyRegistry.h        per-world proxy list + plugin frustum culling (POD snapshots)
│   │   ├── AtmosphereWorldSubsystem.h       per-world registry + owns the view extension
│   │   ├── PlanetAtmosphereViewExtension.h  FWorldSceneViewExtension, BeforeDOF post-process hook
│   │   └── PlanetAtmosphereTypes.h          shared types, units, log category
│   └── Private/
│       ├── AtmosphereRenderer.*             RDG pass setup (camera-relative data, dispatch)
│       ├── AtmosphereShaders.*              global shader classes
│       ├── AtmosphereNoiseTextures.*        shared baked 3D noise textures (FRenderResource, baked once on the GPU)
│       ├── AtmosphereStats.h                CPU stat group (`stat PlanetAtmosphere`)
│       └── AtmosphereCVars.*                r.PlanetAtmosphere.* console variables
└── Shaders/Private/
    ├── PlanetAtmosphereCommon.ush           precision-safe ray/sphere math, compositing helpers
    ├── PlanetAtmosphereShaderData.ush       shared view/atmosphere parameters, per-pixel ray setup
    ├── PlanetAtmosphereNoise.ush            noise: procedural (Phase 1) and baked-texture sampling, periodic fBm
    ├── NoiseBake.usf                        bakes one mip of a tileable 3D noise texture
    ├── CloudDensity.ush                     SINGLE SOURCE OF TRUTH for cloud density (see below)
    ├── CloudLighting.ush                    phase function, light march toward the sun, planet shadow
    ├── RaymarchSchedule.ush                 where the samples go along a ray: uniform / camera-centered steps, jitter
    ├── CloudRaymarch.usf                    cloud raymarch (Final Clouds + Density / Cloud Height / Ray Steps views)
    └── AtmosphereBoundsDebug.usf            Atmosphere Bounds debug view
```

## Installation

1. Copy the `PlanetAtmosphere` folder to your project's `Plugins` directory
2. Regenerate project files
3. Build the project
4. Enable the plugin in the Unreal Editor

## Units

Component parameters are in **meters**; everything render-side is in Unreal Units (cm).
The actor's scale is ignored — radii are absolute.

## Console variables

| CVar | Default | Meaning |
|---|---|---|
| `r.PlanetAtmosphere.Enable` | 1 | Master switch (0 = nothing is dispatched) |
| `r.PlanetAtmosphere.DebugMode` | 0 | 0 = Final Clouds, 1 = Atmosphere Bounds, 2 = Density, 3 = Cloud Height, 4 = Ray Steps |
| `r.PlanetAtmosphere.DebugIntensity` | 1.0 | Brightness of the Atmosphere Bounds overlay |
| `r.PlanetAtmosphere.MaxVisible` | 16 | Max atmospheres per view (closest first) |
| `r.PlanetAtmosphere.DebugPlanetSurface` | 1 | Placeholder planet surface for levels without terrain |
| `r.PlanetAtmosphere.CloudAmbientIntensity` | 0.1 | Ambient (sky) light on clouds as a fraction of the sun; fades out at night |
| `r.PlanetAtmosphere.LightSteps` | 6 | Light-march samples toward the sun (cloud self-shadowing) |
| `r.PlanetAtmosphere.StepDistribution` | 1 | 0 = uniform steps (Phase 1), 1 = camera-centered: small near the camera, larger far away |
| `r.PlanetAtmosphere.StepNearDistance` | 1.0 | Camera-centered steps grow with (distance + this × cloud layer thickness) |
| `r.PlanetAtmosphere.StepRatioMax` | 4 | Camera-centered: max ratio last step / first step of one ray (protects far clouds); ≤ 1 = no cap |
| `r.PlanetAtmosphere.Jitter` | 2 | 0 = off (banding), 1 = static per-pixel pattern, 2 = animated per frame (averaged by TSR while still) |
| `r.PlanetAtmosphere.EmptySpaceSkip` | 0 | Coarse probes over N steps in clear air (2..8); off by default — loses thin clouds |
| `r.PlanetAtmosphere.MinTransmittance` | 0.01 | The view ray stops below this transmittance |
| `r.PlanetAtmosphere.NoiseSource` | 1 | 0 = procedural Phase 1 noise (reference / fallback), 1 = baked 3D textures |
| `r.PlanetAtmosphere.NoiseFootprintScale` | 0.125 | Density LOD: noise octaves fade at this fraction of the pixel footprint; sub-pixel detail is averaged by TSR over frames, so distant planets keep their clouds. 1 = Phase 1 (distant clouds fade out) |
| `r.PlanetAtmosphere.LOD` | 1 | Screen-space LOD: fewer raymarch / light steps for atmospheres small on screen |
| `r.PlanetAtmosphere.LOD.FullDetailRadius` | 400 | Radius on screen (px, render resolution) from which full detail is used |
| `r.PlanetAtmosphere.LOD.MinDetailRadius` | 50 | Radius at and below which minimum detail is used (log2 interpolation in between) |
| `r.PlanetAtmosphere.LOD.MinStepFraction` | 0.25 | Fraction of Raymarch Steps at minimum detail (at least 4) |
| `r.PlanetAtmosphere.LOD.MinLightSteps` | 2 | Light-march steps at minimum detail |

## Sun

The clouds are lit by one Directional Light per level: the first visible one with **Atmosphere Sun Light**
enabled and index 0, otherwise the first visible Directional Light. Its direction, color, temperature and
intensity (lux) are used; the result is pre-exposed like the rest of the scene.

## Debug views and profiling

| `DebugMode` | View |
|---|---|
| 0 | Final clouds |
| 1 | Atmosphere Bounds: planet sphere (green), atmosphere shell (blue), cloud shell (white) |
| 2 | Density: optical depth along the view ray (black → red → yellow → white) |
| 3 | Cloud Height: where in the layer the visible clouds are (blue = bottom, green = middle, red = top) |
| 4 | Ray Steps: density-function calls per pixel incl. light march and empty-space probes — the cost map (white = 64 × (1 + LightSteps)) |

- GPU: `stat gpu` → **PlanetAtmosphere** (all plugin passes of a view); `ProfileGPU` shows the individual passes.
- CPU: `stat PlanetAtmosphere` → Find Sun Light (GT), Gather Visible Atmospheres (RT), Setup Passes (RT).
- Per-frame log: `log LogPlanetAtmosphere Verbose`; screen radius and LOD steps per atmosphere: `log LogPlanetAtmosphere VeryVerbose`.

## Noise textures

Base shape (128³, 5 mips) and erosion (64³, 4 mips) noise are tileable 3D R16F textures, 5 392 384 bytes of texel
data in total. They are baked once on the GPU on first use (`PlanetAtmosphere.BakeNoise` passes; the log prints the
actual allocation), shared by all worlds and planets, and released at module shutdown. Every mip holds the noise
with the octaves that survive one texel of footprint, so the mip level replaces the per-octave fade of the
procedural noise. The weather mask stays procedural (planet-unique). `NoiseSource 0` restores the Phase 1 noise.

## Single source of truth for cloud density

`Shaders/Private/CloudDensity.ush` is the only place where the cloud density formula exists.
Raymarch, debug views and (later) cloud shadows call `PA_SampleCloudDensity()` / `PA_CloudHeightFraction()`
and never re-implement any part of it. Cheaper variants go through the LOD (footprint) argument of the same function.

## Current Status

**Phase 2 — Step 12: density LOD that keeps the cloud cover of distant planets**
- Noise octaves fade at 1/8 of the pixel footprint; TSR's per-frame sub-pixel jitter + accumulation average the detail

**Phase 2 — Step 11: screen-space LOD, Phase 2 final profiling**
- Raymarch and light-march steps scale with the atmosphere's radius on screen (full detail from 400 px)

**Phase 2 — Step 10: baked 3D noise textures**
- Base-shape and erosion noise from shared baked textures (`NoiseSource 1`), procedural Phase 1 noise kept as `NoiseSource 0`

**Phase 2 — Step 9: sample distribution, jitter, early exit**
- Camera-centered step distribution (steps grow with distance, capped per ray), uniform kept as an option
- Per-pixel interleaved-gradient-noise jitter inside each step, animated per frame
- Early exit: light march stops when the sun is blocked; no light march for samples that cannot change the pixel;
  configurable minimum transmittance of the view ray
- Optional empty-space skipping (off by default)

Phase 1 (done): plugin + actor/component/world subsystem, multi-planet registry with frustum culling,
precision-safe camera-relative math (Earth scale), analytical planet/atmosphere/cloud-shell intersections,
analytical cloud density (single source of truth), raymarch, sun lighting with planet shadow, debug views, profiling.

Next: Phase 2.5 — atmospheric scattering (sky from the surface, limb glow from orbit, aerial perspective).
Then Phase 2.5 — atmospheric scattering (sky, limb glow, aerial perspective).

## Dependencies

- Unreal Engine 5.6
- Core, CoreUObject, Engine, Projects, RenderCore, Renderer, RHI

## License

MIT — see [LICENSE](LICENSE).
