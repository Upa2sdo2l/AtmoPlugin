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
    ├── AtmosphereScattering.ush             atmosphere: densities, phase functions, transmittance-LUT mapping, single scattering
    ├── TransmittanceLut.usf                 per-frame transmittance LUT atlas (256 x 64 per visible planet)
    ├── MultipleScatteringLut.usf            per-frame multiple-scattering LUT atlas (64 x 32 per visible planet, Hillaire 2020)
    ├── CloudRaymarch.usf                    clouds + atmosphere (Final + Density / Cloud Height / Ray Steps / Atmosphere Only / LUT views)
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
Atmosphere scattering coefficients are **per kilometer**, scale heights and layer altitudes in meters above
`AtmosphereBottomRadius`.

## Atmosphere parameters (Details Panel → Atmosphere Scattering)

Defaults are Earth's (the same values as UE SkyAtmosphere), but there is no built-in "Earth mode": every value can be
changed in the editor to get an Earth-like, Mars-like or completely fictional atmosphere.
As in UE SkyAtmosphere, each coefficient is **Color × Scale**: the color (0..1) is the visible tint, the scale is the
strength per km. Change the scale to make the medium denser / thinner, the color to change its hue.

| Property | Default | Meaning |
|---|---|---|
| `RayleighScatteringColor` × `RayleighScatteringScale` | (0.175, 0.410, 1.0) × 0.0331 /km | Molecules: sky color, red sunsets |
| `RayleighScaleHeight` | 8000 m | Exponential density profile |
| `MieScatteringColor` × `MieScatteringScale` | white × 0.003996 /km | Aerosols / dust / haze: whitish haze, glow around the sun |
| `MieAbsorptionColor` × `MieAbsorptionScale` | white × 0.000444 /km | Light absorbed by aerosols |
| `MieScaleHeight` | 1200 m | Exponential density profile |
| `MieAnisotropy` | 0.8 | Cornette-Shanks g: 0 = isotropic, → 1 = strong forward glow |
| `OzoneAbsorptionColor` × `OzoneAbsorptionScale` | (0.346, 1.0, 0.045) × 0.001881 /km | At the peak of the ozone layer |
| `OzoneLayerAltitude` / `OzoneLayerWidth` | 25000 / 30000 m | Tent profile: 1 at the peak, 0 at ± width / 2 |
| `SurfaceAlbedo` | (0.04, 0.06, 0.09) | Placeholder planet surface and the ground bounce inside multiple scattering |

The medium is integrated only up to its **effective top** = min(`AtmosphereTopRadius`,
12 × the larger scale height, top of the ozone layer) — ~96 km for Earth. A geometric shell thicker than that
changes nothing (the air above holds ~6·10⁻⁶ of the column) and does not dilute the view-ray samples.

## Console variables

| CVar | Default | Meaning |
|---|---|---|
| `r.PlanetAtmosphere.Enable` | 1 | Master switch (0 = nothing is dispatched) |
| `r.PlanetAtmosphere.DebugMode` | 0 | 0 = Final, 1 = Atmosphere Bounds, 2 = Density, 3 = Cloud Height, 4 = Ray Steps, 5 = Atmosphere Only, 6 = Transmittance LUT, 7 = Multiple-Scattering LUT |
| `r.PlanetAtmosphere.DebugIntensity` | 1.0 | Brightness of the Atmosphere Bounds overlay |
| `r.PlanetAtmosphere.MaxVisible` | 16 | Max atmospheres per view (closest first) |
| `r.PlanetAtmosphere.DebugPlanetSurface` | 1 | Placeholder planet surface for levels without terrain |
| `r.PlanetAtmosphere.CloudAmbientIntensity` | 0.1 | Ambient light on clouds as a fraction of the sun; only when the Step 15 sky ambient is off (`Atmosphere 0` or `MultipleScattering 0`) |
| `r.PlanetAtmosphere.CloudSkyAmbientScale` | 1.0 | Global multiplier of the per-planet **Cloud Sky Ambient Scale** (see below); 0.2 × the default 5 = physical ×1 |
| `r.PlanetAtmosphere.LightSteps` | 6 | Light-march samples toward the sun (cloud self-shadowing) |
| `r.PlanetAtmosphere.StepDistribution` | 1 | 0 = uniform steps (Phase 1), 1 = camera-centered: small near the camera, larger far away |
| `r.PlanetAtmosphere.StepNearDistance` | 1.0 | Camera-centered steps grow with (distance + this × cloud layer thickness) |
| `r.PlanetAtmosphere.StepRatioMax` | 4 | Camera-centered: max ratio last step / first step of one ray (protects far clouds); ≤ 1 = no cap |
| `r.PlanetAtmosphere.Jitter` | 2 | 0 = off (banding), 1 = static per-pixel pattern, 2 = animated per frame (averaged by TSR while still) |
| `r.PlanetAtmosphere.EmptySpaceSkip` | 0 | Coarse probes over N steps in clear air (2..8); off by default — loses thin clouds |
| `r.PlanetAtmosphere.MinTransmittance` | 0.01 | The view ray stops below this transmittance |
| `r.PlanetAtmosphere.NoiseSource` | 1 | 0 = procedural Phase 1 noise (reference / fallback), 1 = baked 3D textures |
| `r.PlanetAtmosphere.NoiseFootprintScale` | 0.25 | Density LOD: noise octaves fade at this fraction of the pixel footprint; sub-pixel detail is averaged by TSR over frames, so distant planets keep their clouds. 1 = Phase 1 (distant clouds fade out) |
| `r.PlanetAtmosphere.LightLOD` | 1 | Fewer light-march steps where the pixel is large vs the cloud layer (sub-pixel self-shadowing) |
| `r.PlanetAtmosphere.LightLOD.FullDetailFootprint` | 0.0625 | Pixel footprint (× layer thickness) up to which the full LightSteps are used |
| `r.PlanetAtmosphere.LightLOD.MinDetailFootprint` | 1.0 | Pixel footprint (× layer thickness) from which `LightLOD.MinLightSteps` are used |
| `r.PlanetAtmosphere.LightLOD.MinLightSteps` | 2 | Light steps for far samples |
| `r.PlanetAtmosphere.Atmosphere` | 1 | Atmosphere single scattering (sky, limb, aerial perspective over the surface / scene). 0 = clouds only (Phase 2 image) |
| `r.PlanetAtmosphere.Atmosphere.MultipleScattering` | 1 | Multiple scattering (all orders ≥ 2, Hillaire LUT, ground bounce with `SurfaceAlbedo`). 0 = single scattering only (Step 13 image, MS LUT pass skipped) |
| `r.PlanetAtmosphere.Atmosphere.Steps` | 16 | Atmosphere samples per view ray (quadratic from inside the atmosphere, uniform from outside), 4..64 |
| `r.PlanetAtmosphere.LOD` | 1 | Screen-space LOD: fewer raymarch / light steps for atmospheres small on screen |
| `r.PlanetAtmosphere.LOD.FullDetailRadius` | 400 | Radius on screen (px, render resolution) from which full detail is used |
| `r.PlanetAtmosphere.LOD.MinDetailRadius` | 50 | Radius at and below which minimum detail is used (log2 interpolation in between) |
| `r.PlanetAtmosphere.LOD.MinStepFraction` | 0.25 | Fraction of Raymarch Steps at minimum detail (at least 4) |
| `r.PlanetAtmosphere.LOD.MinLightSteps` | 2 | Light-march steps at minimum detail |

## Clouds through the atmosphere (Step 15)

With `r.PlanetAtmosphere.Atmosphere 1` the clouds of each planet are lit and seen through its atmosphere:
- **Sunlight** reaches every cloud sample through the atmosphere (transmittance LUT): orange-red at sunset, dark red
  at the terminator; the planet shadow is the atmosphere's horizon (the cloud light march is skipped where the sun
  is not visible).
- **Sky ambient** = sky radiance around the sample from the multiple-scattering LUT (blue by day, colored at
  sunset, dark at night) × **Cloud Sky Ambient Scale** (Details panel → Clouds, default **5**) ×
  `r.PlanetAtmosphere.CloudSkyAmbientScale`.
  > **Cloud Sky Ambient Scale is a temporary artistic compensation until Phase 7.** The clouds do not have multiple
  > scattering inside them yet; the physically correct value 1 makes them far too dark (from orbit ~1/3 of the
  > Phase 2 brightness). ~5 keeps the daytime brightness close to Phase 2. Phase 7 brings it towards 1 or replaces it
  > with a full in-cloud multiple-scattering model.
- **Aerial perspective**: the atmosphere along the view ray is split at the clouds' contribution-weighted mean depth;
  the part in front attenuates the clouds and adds haze, the part behind is seen through them
  (CPU prototype vs a fully interleaved reference: 0.2–0.6 % mean luminance error).

## Sun

The clouds are lit by one Directional Light per level: the first visible one with **Atmosphere Sun Light**
enabled and index 0, otherwise the first visible Directional Light. Its direction, color, temperature and
intensity (lux) are used; the result is pre-exposed like the rest of the scene.

## Debug views and profiling

| `DebugMode` | View |
|---|---|
| 0 | Final: clouds + atmosphere |
| 1 | Atmosphere Bounds: planet sphere (green), atmosphere shell (blue), cloud shell (white) |
| 2 | Density: optical depth along the view ray (black → red → yellow → white) |
| 3 | Cloud Height: where in the layer the visible clouds are (blue = bottom, green = middle, red = top) |
| 4 | Ray Steps: density-function calls per pixel incl. light march and empty-space probes — the cost map (white = 64 × (1 + LightSteps)) |
| 5 | Atmosphere Only: the final image without clouds |
| 6 | Transmittance LUT: final image + the LUT atlas at 2× in the top-left corner (one 256 × 64 block per planet, nearest planet on top; x = view zenith angle, y = altitude) |
| 7 | Multiple-Scattering LUT: final image + the MS LUT atlas at 4× in the top-left corner, ×25 (one 64 × 32 block per planet; x = sun zenith from below the horizon (left) to overhead (right), y = altitude, ground at the top) |

- GPU: `stat gpu` → **PlanetAtmosphere** (noise bake + raymarch / debug pass of a view),
  **PlanetAtmosphere.TransmittanceLut** (Step 13) and **PlanetAtmosphere.MultipleScatteringLut** (Step 14); total = sum. `ProfileGPU` shows the individual passes.
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

**Phase 2.5 — Step 15: clouds through the atmosphere**
- Sunlight on the clouds through the atmosphere (transmittance LUT); planet shadow = the atmosphere's horizon
- Sky ambient on the clouds from the multiple-scattering LUT × Cloud Sky Ambient Scale (temporary, default 5, until Phase 7)
- Aerial perspective on the clouds: atmosphere split at the clouds' mean depth (one extra atmosphere sample)

**Phase 2.5 — Step 14: multiple scattering**
- Hillaire 2020 multiple-scattering LUT, 64 (sun zenith) × 32 (altitude) per planet, 64 directions × 20 steps per texel,
  isotropic higher orders summed as 1 / (1 − f_ms), ground bounce with `SurfaceAlbedo`; per-frame atlas
- The view ray adds (σs Rayleigh + σs Mie) × Ψms per sample: brighter, less saturated sky, whiter horizon,
  softer sunset ring, lit twilight sky opposite the sun
- Known limitation (method): at twilight and on the night side the surface albedo does not brighten the sky
  (only the directly sunlit ground near each point is modelled); ≲ 1–2 % at default albedo

**Phase 2.5 — Step 13: atmosphere single scattering**
- Rayleigh + Mie (Cornette-Shanks) + ozone, all parameters on the component (Earth defaults, no presets)
- Transmittance LUT 256 × 64 per visible planet (Bruneton parameterization written with altitudes — exact in float32
  at Earth scale), rebuilt every frame into a transient atlas
- 16 view-ray samples: quadratic from inside the atmosphere, uniform from outside; energy-conserving; hard planet shadow
- Sky from the surface, limb glow from orbit, aerial perspective over the placeholder surface and scene geometry;
  the placeholder surface is lit by sunlight through the atmosphere
- (Step 15 lights the clouds through the atmosphere and puts the atmosphere in front of them)

**Phase 2 — Step 12: density LOD that keeps the cloud cover of distant planets**
- Noise octaves fade at 1/4 of the pixel footprint; TSR's per-frame sub-pixel jitter + accumulation average the detail
- Light-march steps reduced by the true pixel footprint (far clouds: 6 -> 2..4 steps)

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

Next: Step 16 — Phase 2.5 profiling / optimizations (e.g. caching the LUTs across frames).

## Dependencies

- Unreal Engine 5.6
- Core, CoreUObject, Engine, Projects, RenderCore, Renderer, RHI

## License

MIT — see [LICENSE](LICENSE).
