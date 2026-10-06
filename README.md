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
    └── AtmosphereBoundsDebug.usf            Step 5 debug visualization (compute)
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
| `r.PlanetAtmosphere.Enable` | 1 | Master switch |
| `r.PlanetAtmosphere.DebugMode` | 1 | 0 = off, 1 = Atmosphere Bounds |
| `r.PlanetAtmosphere.DebugIntensity` | 1.0 | Brightness of debug overlays (HDR, before exposure/tonemap) |
| `r.PlanetAtmosphere.MaxVisible` | 16 | Max atmospheres per view (closest first) |

Per-frame diagnostics: `log LogPlanetAtmosphere Verbose`.

## Current Status

**Phase 1 — Step 5: first RDG pass (Atmosphere Bounds debug)**
- `SubscribeToPostProcessingPass(BeforeDOF)` — public API, HDR scene color + depth, before DOF/TSR/tonemap
- One compute pass: analytical planet sphere / atmosphere shell / cloud shell, depth-clipped, multi-planet (near → far)
- Camera-relative, precision-safe at Earth scale: camera offset and altitudes computed in double on the CPU

Next: Step 6 — analytical cloud density + basic raymarch.

## Dependencies

- Unreal Engine 5.6
- Core, CoreUObject, Engine, Projects, RenderCore, Renderer, RHI

## License

MIT — see [LICENSE](LICENSE).
