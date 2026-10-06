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
│       └── AtmosphereCVars.*                r.PlanetAtmosphere.* console variables
└── Shaders/Private/
    ├── PlanetAtmosphereCommon.ush           precision-safe ray/sphere math, compositing helpers
    ├── PlanetAtmosphereShaderData.ush       shared view/atmosphere parameters, per-pixel ray setup
    ├── PlanetAtmosphereNoise.ush            noise primitives (Phase 1: procedural)
    ├── CloudDensity.ush                     SINGLE SOURCE OF TRUTH for cloud density (see below)
    ├── CloudRaymarch.usf                    cloud raymarch (Final Clouds / Density views)
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
| `r.PlanetAtmosphere.DebugMode` | 0 | 0 = Final Clouds, 1 = Atmosphere Bounds, 2 = Density |
| `r.PlanetAtmosphere.DebugIntensity` | 1.0 | Brightness of the Atmosphere Bounds overlay |
| `r.PlanetAtmosphere.MaxVisible` | 16 | Max atmospheres per view (closest first) |
| `r.PlanetAtmosphere.DebugPlanetSurface` | 1 | Placeholder planet surface for levels without terrain |
| `r.PlanetAtmosphere.CloudAmbientIntensity` | 1.0 | Temporary ambient-only cloud lighting (Phase 1) |

Per-frame diagnostics: `log LogPlanetAtmosphere Verbose`.

## Single source of truth for cloud density

`Shaders/Private/CloudDensity.ush` is the only place where the cloud density formula exists.
Raymarch, debug views and (later) cloud shadows call `PA_SampleCloudDensity()` / `PA_CloudHeightFraction()`
and never re-implement any part of it. Cheaper variants go through the LOD (footprint) argument of the same function.

## Current Status

**Phase 1 — Step 6: cloud density + basic raymarch**
- Analytical density: height profile → weather/coverage mask on the sphere → base shape → erosion; evaluated in the planet-local frame
- Pixel-footprint LOD: noise octaves smaller than a pixel fade out (no sparkle from orbit / far away)
- Raymarch over up to two cloud-shell segments, uniform steps (`Raymarch Steps`), Beer–Lambert, ambient-only lighting
- Component: `Cloud Shape Scale` (m), `Cloud Erosion`; `Cloud Coverage` / `Cloud Density` now drive the clouds

Done before: Step 5 — first RDG pass (BeforeDOF hook, Atmosphere Bounds), precision-safe camera-relative math.

Next: Step 7 — sun lighting (Directional Light), phase function, light march, planet shadow.

## Dependencies

- Unreal Engine 5.6
- Core, CoreUObject, Engine, Projects, RenderCore, Renderer, RHI

## License

MIT — see [LICENSE](LICENSE).
