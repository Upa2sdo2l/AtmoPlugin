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
│   │   ├── PlanetAtmosphereTypes.h          shared types, units, log category
│   │   └── PlanetWeatherTypes.h             weather parameters (plain data, model units)
│   └── Private/
│       ├── AtmosphereRenderer.*             RDG pass setup (camera-relative data, dispatch)
│       ├── AtmosphereShaders.*              global shader classes
│       ├── AtmosphereNoiseTextures.*        shared baked 3D noise textures (FRenderResource, baked once on the GPU)
│       ├── AtmosphereLutCache.*             atmosphere LUTs cached across frames (FRenderResource, 32-slot pools)
│       ├── AtmosphereTemporal.*             temporal history per view (FRenderResource): reprojection matrices, history textures
│       ├── AtmosphereCloudShadows.*         cloud shadow cascades per view (FRenderResource, Phase 4)
│       ├── AtmosphereWeatherModel.*         weather model C, CPU half: storm sequence and time uniforms in double (Phase 5)
│       ├── AtmosphereWeather.*              weather snapshots on the GPU (FRenderResource): atlas, planet slots, time grid
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
    ├── TransmittanceLut.usf                 transmittance LUT (256 x 64 per planet), written into its slot of the cache pool
    ├── MultipleScatteringLut.usf            multiple-scattering LUT (64 x 32 per planet, Hillaire 2020), same pool scheme
    ├── CloudRaymarch.usf                    clouds + atmosphere (Final + Density / Cloud Height / Ray Steps / Atmosphere Only / LUT views) -> luminance + transmittance
    ├── AtmosphereTemporal.usf               temporal accumulation: reprojection (camera + planet motion), clamp, blend
    ├── AtmosphereComposite.usf              applies the raymarch result to the scene: scene x transmittance + luminance
    ├── WeatherModel.ush                     weather model C, GPU half (pure function; also compiles as C++ for verification)
    ├── WeatherGenerate.usf                  one weather snapshot of one planet (cube faces) into the weather atlas
    ├── WeatherCommon.ush                    reading the weather atlas (cube-sphere mapping, snapshot interpolation)
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
| `r.PlanetAtmosphere.DebugMode` | 0 | 0 = Final, 1 = Atmosphere Bounds, 2 = Density, 3 = Cloud Height, 4 = Ray Steps, 5 = Atmosphere Only, 6 = Transmittance LUT, 7 = Multiple-Scattering LUT, 8 = Temporal Weight, 9 / 10 / 11 = Shadow Cascade 0 / 1 / 2, 12 = Cloud Shadow Usage, 13 = Surface Cloud Shadow, 14 = Weather Coverage |
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
| `r.PlanetAtmosphere.Temporal` | 1 | Temporal accumulation (Step 18): reprojected history of previous frames (camera and planet motion), clamped to the current 3×3 neighbourhood. Debug modes 0 / 5 / 8, perspective views with a persistent view state (not scene captures / reflections / orthographic) |
| `r.PlanetAtmosphere.Temporal.CurrentFrameWeight` | 0.1 | Minimum weight of the new frame (0.01..1): lower = smoother, more lag in motion |
| `r.PlanetAtmosphere.Temporal.ClampGamma` | 1.25 | History clamped to mean ± gamma × std of the current neighbourhood: lower = less ghosting, more noise |
| `r.PlanetAtmosphere.Temporal.Interleave` | 3 | Interleaved rendering (Step 19): the raymarch traces one pixel per N × N block per frame, the temporal pass reconstructs the rest. 1 = every pixel, 2, 3 (default, ~1/9 of the raymarch cost), 4 |
| `r.PlanetAtmosphere.Temporal.StaticClampGamma` | 8 | Interleaved: clamp gamma for pixels that did not move (the coarse sample neighbourhood would keep a static image from converging) |
| `r.PlanetAtmosphere.Temporal.ClampMotionPixels` | 0.5 | Interleaved: motion (pixels) from which `ClampGamma` applies fully |
| `r.PlanetAtmosphere.Temporal.DepthRejectRatio` | 4 | History dropped where its depth differs by more than this factor (disocclusion, camera jumps without a camera cut) |
| `r.PlanetAtmosphere.Atmosphere.LutCache` | 1 | LUTs cached across frames, rebuilt only on change (Step 16). 0 = rebuild every LUT every frame (old cost; for comparison or after `recompileshaders`) |
| `r.PlanetAtmosphere.Atmosphere.Steps` | 16 | Atmosphere samples per view ray (quadratic from inside the atmosphere, uniform from outside), 4..64 |
| `r.PlanetAtmosphere.LOD` | 1 | Screen-space LOD: fewer raymarch / light steps for atmospheres small on screen |
| `r.PlanetAtmosphere.LOD.FullDetailRadius` | 400 | Radius on screen (px, render resolution) from which full detail is used |
| `r.PlanetAtmosphere.LOD.MinDetailRadius` | 50 | Radius at and below which minimum detail is used (log2 interpolation in between) |
| `r.PlanetAtmosphere.LOD.MinStepFraction` | 0.25 | Fraction of Raymarch Steps at minimum detail (at least 4) |
| `r.PlanetAtmosphere.LOD.MinLightSteps` | 2 | Light-march steps at minimum detail |
| `r.PlanetAtmosphere.CloudShadows` | 1 | Cloud shadow cascades of the primary planet (Phase 4): generated, kept across frames, shown in debug modes 9–11 |
| `r.PlanetAtmosphere.CloudShadows.Lighting` | 1 | Sunlight on the primary planet's clouds = short local march × cascades (Step 23); 0 = the full light march everywhere (A/B) |
| `r.PlanetAtmosphere.CloudShadows.LocalMarchSteps` | 3 | Samples of the local march toward the sun (1 = fast, 2 = compromise, 3 = default, up to 8); reduced for distant samples by `r.PlanetAtmosphere.LightLOD` |
| `r.PlanetAtmosphere.CloudShadows.LocalMarchLength` | 1.0 | Length of the local march, km (0.1..20) |
| `r.PlanetAtmosphere.CloudShadows.SunRebuildAngle` | 0.1 | Sun movement in the planet frame (degrees; time of day, planet rotation) after which the second set of cascades is built in the background and crossfaded in (Step 25). Shadows lag the sun by at most this angle |
| `r.PlanetAtmosphere.CloudShadows.CrossfadeFrames` | 16 | Frames of the crossfade to the rebuilt set (0 = switch at once) |
| `r.PlanetAtmosphere.CloudShadows.Surface` | 1 | Cloud shadows on the direct sunlight of the placeholder surface (Step 24); 0 = off (A/B). Scene geometry is not shadowed |
| `r.PlanetAtmosphere.CloudShadows.SurfaceMarchSteps` | 12 | Minimum samples of the march from a surface point through the cloud layer toward the sun, where the cascades are too coarse (from high up / orbit) and on planets without cascades (1..64; prototype worst-case error 8 → 7.4, 12 → 3.5, 16 → 2.4) |
| `r.PlanetAtmosphere.CloudShadows.SurfaceMarchMaxStep` | 0.125 | Longest step of that march, × `CloudShapeScale` (1 km at 8 km): long low-sun paths get more steps, otherwise the shadows turn into too-bright "ladders" (sun 8°: 12 fixed steps 9.5, ≤ 1 km steps 0.8). 0 = always `SurfaceMarchSteps` |
| `r.PlanetAtmosphere.CloudShadows.SurfaceMarchMaxSteps` | 48 | Upper limit of those steps |
| `r.PlanetAtmosphere.CloudShadows.SurfaceMarchJitter` | 1 | Per-pixel animated jitter of those samples (0 = step centres, A/B) |
| `r.PlanetAtmosphere.CloudShadows.SurfaceMaxTexel` | 0.0625 | Coarsest cascade texel used on the surface, × the planet's `CloudShapeScale` (1/16 = 500 m at 8 km); coarser → march. 0 = always march |
| `r.PlanetAtmosphere.CloudShadows.Resolution` | 512 | Texels per cascade side (multiple of 32, 128..1024); atlas Res × 6 Res RGBA16F (two sets of 3 cascades, Step 25), 12 MB per view at 512 |
| `r.PlanetAtmosphere.CloudShadows.GenerationSteps` | 32 | Density samples per cascade texel along the sun through the cloud shell (8..128) |
| `r.PlanetAtmosphere.CloudShadows.UpdateBudget` | 32 | Tiles of 32 × 32 texels generated per view and frame (0..768; 0 = frozen). The GPU cost of the cascades: RTX 3050 frame peaks 8 → 0.11 ms, 16 → 0.14 ms, 32 → 0.23 ms; 32 fills all cascades in 24 frames |
| `r.PlanetAtmosphere.CloudShadows.MinExtent` | 8 | Half-size of cascade 0 near the cloud layer, km; grows with the camera height in powers of two; cascade i = × 4^i |
| `r.PlanetAtmosphere.Weather` | 1 | Planetary weather (Phase 5): computed per planet on the GPU; Step 27 shows it only in `DebugMode 14`. 0 = no weather passes (atlas freed after 120 frames) |
| `r.PlanetAtmosphere.Weather.TimeScale` | 4 | Default weather clock: game seconds per world second (4 = one 24 h day in 6 real hours; 0 = frozen). Overridden by `SetWeatherTimeScale` |
| `r.PlanetAtmosphere.Weather.TimeOffsetHours` | 0 | Game hours added to the weather clock (testing: jump in time) |
| `r.PlanetAtmosphere.Weather.Resolution` | 256 | Texels per cube-face side (64..512): ~39 km per texel on an Earth-size planet; 9.4 MB per planet at 256 |
| `r.PlanetAtmosphere.Weather.MaxPlanets` | 4 | Planets with weather per view, nearest first (1..8); the shared atlas grows to the number seen in a frame by all views (editor + PIE), up to 2 × this |
| `r.PlanetAtmosphere.Weather.SnapshotInterval` | 600 | Game seconds between weather snapshots (10..86400); the image interpolates between two |
| `r.PlanetAtmosphere.Weather.FacesPerFrame` | 1 | Cube faces of the next snapshot built per frame and planet in the background (1..6) |

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

## Weather (Phase 5)

Deterministic planetary weather, **model C** (decision AD-30): a zonal climatology (Hadley / Ferrel / polar cells,
trades, westerlies, ascent, humidity; scaled by the rotation rate and planet size; seasonal), synoptic noise carried by the
zonal wind, planetary waves on the storm tracks, convective clusters along a meandering ITCZ, and storm objects
(extratropical cyclones with a spiral arm, core and fronts; tropical cyclones with an eye) that are born, drift, mature and
decay from a deterministic sequence. Every field is a pure function of (direction, weather time, the planet's **Weather**
parameters, planet radius): two machines with the same weather time and parameters show the same weather, nothing drifts
over long times.

- **Parameters** (Details → Weather): seed, rotation period (h), retrograde, axial tilt, year length (days), season
  phase, mean temperature (°C), equator–pole difference (K), mean humidity, cyclone lifetime (days, default 5, 3–7 for
  tests), cyclones per hemisphere, cyclone radius (m), tropical cyclones per hemisphere, wind scale. Earth-like
  defaults. Earth-like bands also need an Earth-like size and rotation: the default 1000 km planet rotating in 24 h is
  a slow rotator for its size (Hadley cells reach 60°); use `PlanetRadius` 6,371,000 m or a faster rotation.
- **Weather clock** (per world, game seconds): `AtmosphereWorldSubsystem` → `GetWeatherTime`, `SetWeatherTime`,
  `SetWeatherTimeScale`, `ClearWeatherTimeOverride`, `GetWeatherTimeScale` (Blueprint). Default = world time ×
  `Weather.TimeScale` (pauses with the game). Multiplayer / save games: set the authoritative time; the clock keeps
  running from it.
- **GPU state**: snapshots on a global time grid (`SnapshotInterval`); the two around the weather time are interpolated,
  the next is built in the background (`FacesPerFrame`); a missing one (first frame, time jump, changed parameters) is
  built at once. One RGBA16F atlas (cloud water, humidity, wind east, wind north), equi-angular cube-sphere,
  6 × Res² × 3 snapshots per planet; log `Weather: atlas …` on creation. GPU stat **PlanetAtmosphere.Weather**.
- **Verification** (Step 27): the shader model file and the CPU storm code, compiled on the CPU, match the numpy
  prototype to 1.5e-5 in cloud water (5 parameter sets × 7 times incl. negative and 10-year times; identical storm lists).
- Step 27 does not change the clouds yet: `DebugMode 14` shows the cloud water on the middle of the cloud layer
  (dark blue 0 → white 1, latitude lines every 30°, equator orange). Clouds follow the weather from Step 29.

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
| 8 | Temporal Weight: 1 / accumulated sample weight over the darkened image (green = long history, red = little history: rejected, just started; with interleaving pixels far from this frame's samples stay greener) |
| 12 | Cloud Shadow Usage: lit cloud samples of the primary planet over the darkened image — green = sunlight through the cascades, red = full light march (outside the cascade windows, or a tile not generated yet). Pixels without such samples show the surface: green = cascades, blue = march through the layer (texel too coarse, other planets; blend band at a window edge in between), red = march because the tile is not generated yet |
| 13 | Surface Cloud Shadow: cloud transmittance of the sun path at the visible surface point (white = lit, black = shadowed), shown in front of the clouds; dark blue = surface without direct sun (night side, or `CloudShadows.Surface 0`) |
| 14 | Weather Coverage (Phase 5): the weather cloud water of the nearest planet on the sphere in the middle of its cloud layer, unlit (dark blue = 0 → white = 1), latitude lines every 30° (equator orange); magenta checker = no weather for this planet (`Weather 0`, beyond `Weather.MaxPlanets`) |
| 9 / 10 / 11 | Shadow Cascade 0 / 1 / 2: final image + the cascade of the primary planet in the top-left corner (+V up): white = column lit, dark blue = column shadowed (exp(−optical depth of the cloud shell)), magenta checker = tile not generated yet, red ring = sub-camera point; red square = no cascades this frame |

- GPU: `stat gpu` → **PlanetAtmosphere.Raymarch** (noise bake + raymarch / debug pass of a view; called
  **PlanetAtmosphere** before Step 17), **PlanetAtmosphere.Temporal** (Step 18), **PlanetAtmosphere.Composite** (Step 17: applying the result to the scene),
  **PlanetAtmosphere.TransmittanceLut** (Step 13), **PlanetAtmosphere.MultipleScatteringLut** (Step 14) and
  **PlanetAtmosphere.CloudShadows** (Phase 4: cascade generation, only on frames with work) and **PlanetAtmosphere.Weather**
  (Phase 5: weather snapshots, only on frames with work); total = sum. `ProfileGPU` shows the individual passes.
  Since Step 16 the two LUT stats appear only on frames where a LUT is rebuilt (first frame, a parameter edit, a new planet).
- CPU: `stat PlanetAtmosphere` → Find Sun Light (GT), Gather Visible Atmospheres (RT), Setup Passes (RT), Weather Update (RT).
- Per-frame log: `log LogPlanetAtmosphere Verbose` (incl. LUT cache rebuilds); screen radius and LOD steps per atmosphere: `log LogPlanetAtmosphere VeryVerbose`.

## Noise textures

Base shape (128³, 5 mips) and erosion (64³, 4 mips) noise are tileable 3D R16F textures, 5 392 384 bytes of texel
data in total. They are baked once on the GPU on first use (`PlanetAtmosphere.BakeNoise` passes; the log prints the
actual allocation), shared by all worlds and planets, and released at module shutdown. Every mip holds the noise
with the octaves that survive one texel of footprint, so the mip level replaces the per-octave fade of the
procedural noise. The weather mask stays procedural (planet-unique). `NoiseSource 0` restores the Phase 1 noise.

## Atmosphere LUT cache (Step 16)

The transmittance and multiple-scattering LUTs depend only on the planet radius, the atmosphere bottom / top and the
scattering parameters — not on the camera, the sun, the clouds or the LOD. They live in two persistent pools of
32 slots (transmittance 256 × 2048, multiple scattering 64 × 1024, RGBA16F, ≈ 4.5 MB together; the log prints the actual
allocation), shared by all worlds, views and planets and released at module shutdown. Each slot remembers the exact
parameter values of the atmosphere it holds; a LUT is rebuilt only when an atmosphere gets a slot with no valid LUT
for its values (first use, a parameter edit, a new planet, or more than 32 distinct atmospheres in use — then the least
recently used slot is reused). A static scene therefore renders without any LUT pass. The cache does not notice a
shader recompile (`recompileshaders`): toggle `r.PlanetAtmosphere.Atmosphere.LutCache 0` → `1` or edit any parameter.

## Cloud shadow cascades (Phase 4)

Per view, 3 cloud shadow cascades of the **primary planet** (the one largest on screen; the camera inside an atmosphere
wins; the previous one is kept until another is 1.25× larger, since a switch regenerates the cascades). Each cascade is a sun-aligned orthographic grid in the planet-local frame, a window of Res × Res texels around
the point under the camera, snapped to 32-texel tiles (no shimmering), stored toroidally in one Res × 6 Res RGBA16F atlas (two sets, see below)
(Beer Shadow Map: front and back of the cloud matter along the texel's ray through the cloud shell, optical depth of the
shell, valid flag). Half-size of cascade 0 = `MinExtent` × 2^k, the first power of two above the camera height over the
clouds (at most what lets cascade 2 cover the whole planet); cascade i = × 4^i. Tiles are generated progressively (`UpdateBudget` per frame, nearest to the camera first,
round-robin over the cascades) and kept across frames: a static camera costs nothing, a moving one only the tiles that
enter the window. Tiles not generated yet are marked invalid (the light march is used there, so new regions never cause
a hitch).
Step 25 — two sets: the **front** set lights the image. When the sun direction in the planet frame (time of day, planet
rotation) has moved by more than `SunRebuildAngle`, or the camera height leaves the extent level's range, the **back** set
is built for the new state in the background (after the front's own new tiles) and, once complete, crossfaded in over
`CrossfadeFrames`; then the sets swap. So a rebuild never drops to the light march (which made low-sun clouds 2–20 %
brighter for ~24 frames) and the swap itself (0.4–5 % image change from the rotated texel grid) is spread over the
crossfade. A change of planet, resolution, cloud parameters, noise or extent settings starts both sets over.
Log: `Cloud shadows of view N: atlas …` on creation; per frame (Verbose) generated / invalidated / pending tiles.

Surface (Step 24): the direct sunlight of the placeholder surface is multiplied by the cloud transmittance of its sun
path. Primary planet: the cascades where the texel of the cascade holding the point is at most `SurfaceMaxTexel` ×
`CloudShapeScale`; elsewhere (camera high up, orbit) a march through the whole layer crossing with steps of at most
`SurfaceMarchMaxStep` (`SurfaceMarchSteps`..`SurfaceMarchMaxSteps` samples, jittered per pixel and frame) — coarse cascade texels blur the shadows more than the march errs (prototype: texel 2 km → 6.5, 8 km →
22 vs ~2 for the march). The last tile of the last usable window blends into the march (no hard ring). Other planets
and tiles not generated yet: the march. Only direct sunlight; sky light under clouds and shadows in the air → Phase 7.

## Single source of truth for cloud density

`Shaders/Private/CloudDensity.ush` is the only place where the cloud density formula exists.
Raymarch, debug views and the cloud shadow cascades call `PA_SampleCloudDensity()` / `PA_CloudHeightFraction()`
and never re-implement any part of it. Cheaper variants go through the LOD (footprint) argument of the same function.

## Current Status

**Phase 5 — Step 27: weather GPU state** (in review)
- Weather parameters on the component (Earth-like defaults), weather clock in the world subsystem (Blueprint)
- Model C on the GPU: CPU storm sequence + time uniforms in double, shader model, cube-sphere snapshots on a global
  time grid with interpolation and background build; `DebugMode 14`, `stat gpu` PlanetAtmosphere.Weather
- Verified on the CPU against the numpy prototype (above); atlas sampling at 256: mean |ΔC| 0.001 vs the exact field

**Phase 4 closed (Steps 21–25)** — cloud shadows: 3 sun-aligned cascades per view for the primary planet (Beer Shadow
Map, progressive tile generation, two sets with background rebuild + crossfade), hybrid sun transmittance for the clouds,
shadows on the planet surface. UE measurements (RTX 3050, Interleave 3, PlanetAtmosphere.Raymarch default / without
cloud shadows): sunset in clouds 1.08 / 0.95 ms, low orbit 1.12 / 1.06 ms, far planet 1.36 / 1.14 ms → +0.06…0.22 ms;
CloudShadows 0 ms when static, ~0.12 ms (peak 0.22) while the sun moves; atlas 12 MB per view.

**Phase 4 — Step 25: stability of the cloud shadows**
- Prototype p25: shimmer while the camera moves is negligible (fixed points: mean frame-to-frame change 0.00–0.04,
  p99 0); the problem was the rebuild on a sun change (> 0.25°): ~24 frames of the light march, low-sun clouds 2–20 %
  brighter, every ~15 s with a 6-hour day
- Double-buffered cascades with a crossfade (above); rebuild angle 0.25° → 0.1° (shadows near the ground lag the sun by
  at most that: surface error +0.0–0.4); `CloudShadows.SunRebuildAngle`, `.CrossfadeFrames`, `.SurfaceMarchJitter`
- One primary planet per view kept (user decision): other planets use the marches

**Phase 4 — Step 24: cloud shadows on the planet surface**
- Placeholder surface: direct sun × cloud transmittance; cascades near the camera, a march through the cloud layer where
  their texels are too coarse (from about 60 km up with the defaults) and on other planets, blended at the switch
- Prototype p24/p24b (12 scenes, ground → 2000 km, pixel-averaged): cascades only 0.5–27 (mean |ΔT| × 100), chosen
  rule with 12 steps 0.5–3.5
- `r.PlanetAtmosphere.CloudShadows.Surface` (A/B), `.SurfaceMarchSteps`, `.SurfaceMarchMaxStep`, `.SurfaceMarchMaxSteps`,
  `.SurfaceMaxTexel`, `DebugMode 12` (surface colours) and `13`
- UE test (RTX 3050): Raymarch in orbit 1.54 (off) / 1.74 / 1.82 / 1.97 ms at 8 / 12 / 16 steps; near the clouds 1.25 →
  1.30 ms. Fix after the test: at low sun the fixed 12 steps (4–7 km each) left "ladder" shadows that were too bright →
  steps of at most 1 km (12..48) + per-pixel jitter

**Phase 4 — Step 23: the cascades light the clouds**
- Sunlight on every lit cloud sample of the primary planet = local march (3 quadratic samples over 1 km toward the sun) ×
  the cascades from the end of that march on (Beer Shadow Map, 2 × 2 texels blended); the full light march of Steps 7–12
  only where no cascade texel is available (outside the windows, tile not generated yet) and for the other planets
- Removes the light leaks of the old march at low sun (21 km cap, coarse far steps); fewer density calls per lit sample
- `r.PlanetAtmosphere.CloudShadows.Lighting` (A/B), `.LocalMarchSteps`, `.LocalMarchLength`, `DebugMode 12`

**Phase 4 — Step 22: cloud shadow cascades — generation, storage, update queue, debug views**
- Cascades as described above; the image does not use them yet (Step 23: short local march + cascade for the rest of
  the sun path, decision from the Step 21 prototype)
- `DebugMode 9 / 10 / 11`, `stat gpu` → PlanetAtmosphere.CloudShadows, CVars `r.PlanetAtmosphere.CloudShadows.*`
- Fix (Step 19 interleave): with N > 1 the step jitter is evaluated on the block coordinate. On the traced pixels
  (stride N) the interleaved gradient noise aliased into persistent screen-space stripes on clouds and crawling
  patterns on far clouds at 3×3

**Phase 4 — Step 21: prototype** (CPU): the current 6-step light march is too bright at low sun (21 km cap, coarse far
steps); cascades alone are too coarse from orbit; chosen: 1 km local march + Beer-shadow-map cascades (mean error 8.5 → 3.4–4.1 %)

**Phase 3 — Step 20: Phase 3 closed** (measurements below, checklist in the project docs)

**Phase 3 — Step 19: interleaved 3×3 rendering**
- The raymarch traces one pixel per N × N block per frame (ordered-dither order rotated by a pseudo-random amount every
  cycle, so every pixel meets the TSR sub-pixel jitter phases whatever the engine's jitter sequence length); the temporal pass reconstructs the full image: bilinear of the block
  samples as the current frame, reprojection data of the nearest sample, new-frame weight × exp(−d²/2·0.5²) by the
  distance to it, clamp to the 3×3 block samples with a motion-adaptive gamma (8 static → 1.25 from 0.5 px of motion)
- Prototype: raymarch cost ~1/9; static scenes converge to the Step 18 quality (≈1.5 s instead of 0.3 s); in motion the
  error is ~2× Step 18 (softer, faint grid), still well below no accumulation at realistic speeds; 2×2 in between
- History: + planet slot R16F (accumulated weight in transmittance.a); N = `r.PlanetAtmosphere.Temporal.Interleave` (live switch)

**Phase 3 — Step 18: temporal history + reprojection**
- Per view history (luminance + transmittance), reprojected with the previous camera and the previous transform of each
  planet (moving / rotating actors), at the clouds' contribution-weighted depth (else surface / scene / atmosphere middle)
- Catmull-Rom history, clamp to the current 3×3 neighbourhood, blend 1/N → 0.1; history dropped on camera cuts, off-screen,
  planet change or depth mismatch (×4); exposure changes compensated
- Prototype (160×90, 16 steps): error vs the converged image 3–5× lower, frame-to-frame flicker 10–13× lower
  (far-planet "salt": 37.6 % → 7.7 %); the raymarch still runs for every pixel (cost +0.2–0.4 ms; savings in Step 19)
- Memory: 2 × RGBA16F per view at the render extent (~33 MB at 1920×1080, twice that while a frame is written);
  histories of views not rendered for 120 frames are freed; a history older than 4 frames, of the other image type
  (final / atmosphere only) or before a camera cut is not reused

**Phase 3 — Step 17: raymarch result separated from the scene**
- The raymarch writes its own pre-exposed luminance + transmittance buffers (every debug view too); a composite pass
  produces scene × transmittance + luminance. Same image as before (up to fp16 rounding); groundwork for the temporal
  history (Step 18) and interleaved rendering (Step 19)

**Phase 2.5 — Step 16: profiling, LUT cache**
- Measured cost (Step 16 measurements): atmosphere +1.7–2.8 ms over clouds only; LUTs were 0.15–0.2 ms per frame
- Transmittance and multiple-scattering LUTs cached across frames (see "Atmosphere LUT cache"): rebuilt only on change

**Phase 2.5 — Step 15: clouds through the atmosphere**
- Sunlight on the clouds through the atmosphere (transmittance LUT); planet shadow = the atmosphere's horizon
- Sky ambient on the clouds from the multiple-scattering LUT × Cloud Sky Ambient Scale (temporary, default 5, until Phase 7)
- Aerial perspective on the clouds: atmosphere split at the clouds' mean depth (one extra atmosphere sample)

**Phase 2.5 — Step 14: multiple scattering**
- Hillaire 2020 multiple-scattering LUT, 64 (sun zenith) × 32 (altitude) per planet, 64 directions × 20 steps per texel,
  isotropic higher orders summed as 1 / (1 − f_ms), ground bounce with `SurfaceAlbedo` (cached since Step 16)
- The view ray adds (σs Rayleigh + σs Mie) × Ψms per sample: brighter, less saturated sky, whiter horizon,
  softer sunset ring, lit twilight sky opposite the sun
- Known limitation (method): at twilight and on the night side the surface albedo does not brighten the sky
  (only the directly sunlit ground near each point is modelled); ≲ 1–2 % at default albedo

**Phase 2.5 — Step 13: atmosphere single scattering**
- Rayleigh + Mie (Cornette-Shanks) + ozone, all parameters on the component (Earth defaults, no presets)
- Transmittance LUT 256 × 64 per visible planet (Bruneton parameterization written with altitudes — exact in float32
  at Earth scale); cached across frames since Step 16
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

Phase 3 closed (Step 20): UE measurements at 1256×756 (RTX 3050), PlanetAtmosphere.Raymarch with Interleave 1 / 2 / 3 / 4:
sunset in clouds 8.08 / 2.10 / 1.04 / 0.62 ms, low orbit 11.59 / 3.09 / 1.53 / 0.97 ms, far planet 6.82 / 1.65 / 0.85 / 0.48 ms;
Temporal 0.26–0.29 ms, Composite 0.06 ms; whole GPU frame 15.2 → 6.9, 19.1 → 7.4, 13.7 → 6.8 ms (Interleave 1 → 3).

Next: Phase 5 — Step 28 (debug views Wind / Humidity / Temperature), Step 29 (weather drives the cloud coverage).
Step 16 part 3 (cheaper cloud/atmosphere coupling) was closed without implementation after Phase 3 (saving ~0.1 ms).

## Dependencies

- Unreal Engine 5.6
- Core, CoreUObject, Engine, Projects, RenderCore, Renderer, RHI

## License

MIT — see [LICENSE](LICENSE).
