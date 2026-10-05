# Planet Atmosphere Plugin

Multi-planet GPU-driven volumetric atmosphere and weather system for Unreal Engine 5.6.

## Structure

```
PlanetAtmosphere/
├── PlanetAtmosphere.uplugin
├── Source/
│   └── PlanetAtmosphere/
│       ├── PlanetAtmosphere.Build.cs
│       ├── Public/
│       │   └── PlanetAtmosphereModule.h
│       └── Private/
│           └── PlanetAtmosphereModule.cpp
└── Shaders/
    └── PlanetAtmosphereCommon.ush
```

## Installation

1. Copy the `PlanetAtmosphere` folder to your project's `Plugins` directory
2. Regenerate project files
3. Build the project
4. Enable the plugin in the Unreal Editor

## Current Status

**Phase 1 - Step 1: Plugin Structure** ✅
- Basic plugin structure created
- Shader directory mapping configured
- Ready for game thread classes

## Dependencies

- Unreal Engine 5.6
- Core, CoreUObject, Engine, RenderCore, Renderer, RHI

## License

Copyright Epic Games, Inc. All Rights Reserved.
