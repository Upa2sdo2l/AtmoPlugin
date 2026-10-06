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
│       ├── AtmosphereStats.h                CPU stat group (`stat PlanetAtmosphere`)
│       └── AtmosphereCVars.*                r.PlanetAtmosphere.* console variables
└── Shaders/Private/
    ├── PlanetAtmosphereCommon.ush           precision-safe ray/sphere math, compositing helpers
    ├── PlanetAtmosphereShaderData.ush       shared view/atmosphere parameters, per-pixel ray setup
    ├── PlanetAtmosphereNoise.ush            noise primitives (Phase 1: procedural)
    ├── CloudDensity.ush                     SINGLE SOURCE OF TRUTH for cloud density (see below)
    ├── CloudLighting.ush                    phase function, light march toward the sun, planet shadow
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
| 4 | Ray Steps: density-function calls per pixel incl. light march — the cost map (white = 64 × (1 + LightSteps)) |

- GPU: `stat gpu` → **PlanetAtmosphere** (all plugin passes of a view); `ProfileGPU` shows the individual passes.
- CPU: `stat PlanetAtmosphere` → Find Sun Light (GT), Gather Visible Atmospheres (RT), Setup Passes (RT).
- Per-frame log: `log LogPlanetAtmosphere Verbose`.

## Single source of truth for cloud density

`Shaders/Private/CloudDensity.ush` is the only place where the cloud density formula exists.
Raymarch, debug views and (later) cloud shadows call `PA_SampleCloudDensity()` / `PA_CloudHeightFraction()`
and never re-implement any part of it. Cheaper variants go through the LOD (footprint) argument of the same function.

## Current Status

**Phase 1 — Step 8: debug views, profiling, Phase 1 final test**
- Cloud Height and Ray Steps debug views (same march, same density calls as the final image)
- GPU stat `PlanetAtmosphere`, CPU stat group `PlanetAtmosphere`

Phase 1 so far: plugin + actor/component/world subsystem, multi-planet registry with frustum culling,
precision-safe camera-relative math (Earth scale), analytical planet/atmosphere/cloud-shell intersections,
analytical cloud density (single source of truth), raymarch, sun lighting with planet shadow.

Next: Phase 2 — variable stepping, jitter, early exit, density optimization (baked 3D noise).

## Dependencies

- Unreal Engine 5.6
- Core, CoreUObject, Engine, Projects, RenderCore, Renderer, RHI

## License

MIT — see [LICENSE](LICENSE).
