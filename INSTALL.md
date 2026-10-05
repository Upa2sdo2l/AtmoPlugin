# Planet Atmosphere Plugin - Installation Guide

## Step 2: Game Thread Classes

### Installation Instructions

**1. Extract the archive**
```
Unzip PlanetAtmosphere_Step2.zip
```

**2. Copy to your project**
```
YourProject/
└── Plugins/
    └── PlanetAtmosphere/    ← Copy the entire folder here
```

**3. Regenerate project files**
- Right-click on `YourProject.uproject`
- Select "Generate Visual Studio project files"

**4. Build the project**
- Open the solution in Visual Studio
- Build (Ctrl+Shift+B)
- Wait for compilation to complete

**5. Start the Editor**
- Launch your project in Unreal Engine 5.6

**6. Enable the plugin (if not auto-enabled)**
- Edit → Plugins
- Search for "Planet Atmosphere"
- Check the box to enable
- Restart editor if prompted

---

## Verification Steps

### Check 1: Plugin Loaded
Open **Output Log** (Window → Developer Tools → Output Log)

Look for:
```
LogTemp: PlanetAtmosphere: Shader directory mapped to [path]
LogTemp: AtmosphereWorldSubsystem: Initialized
```

### Check 2: Actor Available
- In Content Browser, go to "View Options"
- Enable "Show Plugin Content"
- Or simply: Place → search "PlanetAtmosphereActor"

### Check 3: Place Actor in Level
1. **Add Actor**: Place → All Classes → Search "Planet" → **PlanetAtmosphereActor**
2. **Check Output Log**:
   ```
   LogTemp: AtmosphereWorldSubsystem: Registered atmosphere 'PlanetAtmosphereActor_0' (Total: 1)
   ```

### Check 4: Edit Parameters
Select the actor in the World Outliner, check **Details** panel:

**Planet:**
- Planet Radius: 1000000.0

**Atmosphere:**
- Atmosphere Bottom Radius: 1000000.0
- Atmosphere Top Radius: 1100000.0

**Clouds:**
- Cloud Bottom Radius: 1010000.0
- Cloud Top Radius: 1020000.0
- Cloud Coverage: 0.5
- Cloud Density: 1.0

**Rendering:**
- Raymarch Steps: 64

Change any value → Check log for "MarkRenderStateDirty" being called (SceneProxy will be recreated)

### Check 5: Multiple Actors
- Add a second `PlanetAtmosphereActor`
- Check Output Log:
  ```
  LogTemp: AtmosphereWorldSubsystem: Registered atmosphere 'PlanetAtmosphereActor_1' (Total: 2)
  ```

---

## Expected Behavior at Step 2

✅ **Working:**
- Plugin loads without errors
- Actor appears in Place menu
- Actor can be placed in Level
- Subsystem registers/unregisters actors
- SceneProxy is created (visible in debugger)
- Parameters are editable
- Multiple actors work simultaneously

❌ **Not Yet Implemented:**
- No visible rendering (Step 5+ will add shaders)
- No actual atmosphere visuals
- SceneProxy created but not used for rendering yet

---

## Troubleshooting

**Problem: Plugin not found**
- Verify folder structure: `Plugins/PlanetAtmosphere/PlanetAtmosphere.uplugin` exists
- Regenerate project files
- Clean and rebuild

**Problem: Compile errors about TObjectPtr**
- You're using UE 5.6, `TObjectPtr` is correct
- Check Build.cs has all dependencies

**Problem: Actor not in Place menu**
- Enable "Show Plugin Content" in Content Browser settings
- Search "Planet" in Place Actors panel

**Problem: No log messages**
- Check Output Log verbosity (set to "Log" or "Verbose")
- Check Window → Developer Tools → Output Log is open

---

## What Step 2 Delivers

### C++ Classes Created

**Game Thread:**
- `APlanetAtmosphereActor` - Placeable actor
- `UPlanetAtmosphereComponent` - Configuration interface
- `UAtmosphereWorldSubsystem` - Central registration

**Render Thread:**
- `FPlanetAtmosphereSceneProxy` - Thread-safe render state

### Thread Safety

**✅ CORRECT:**
```cpp
// Render Thread reads SceneProxy
FPlanetAtmosphereSceneProxy* Proxy = Component->SceneProxy;
double radius = Proxy->PlanetRadius; // SAFE - immutable
```

**❌ WRONG:**
```cpp
// Render Thread reads Component directly
double radius = Component->PlanetRadius; // CRASH - not thread-safe!
```

### Parameter Flow

```
User changes value in Details
    ↓
PostEditChangeProperty()
    ↓
MarkRenderStateDirty()
    ↓
CreateSceneProxy() (new instance)
    ↓
FPlanetAtmosphereSceneProxy created with new values
    ↓
Old proxy destroyed
```

---

## Next Steps

After confirming Step 2 works:
- **Step 3**: Render Thread (View Extension, visibility culling)
- **Step 4**: Minimal Shader (sphere intersection tests)
- **Step 5**: RDG Pass Setup
- **Step 6**: Basic Raymarch
- **Step 7**: Multi-Planet Support
- **Step 8**: Camera-Relative Coordinates
- **Step 9**: Basic Parameters
- **Step 10**: Final Testing

---

## Files Included in Step 2

```
PlanetAtmosphere/
├── PlanetAtmosphere.uplugin
├── README.md
├── INSTALL.md (this file)
├── Source/
│   └── PlanetAtmosphere/
│       ├── PlanetAtmosphere.Build.cs
│       ├── Public/
│       │   ├── PlanetAtmosphereModule.h
│       │   ├── PlanetAtmosphereActor.h
│       │   ├── PlanetAtmosphereComponent.h
│       │   ├── PlanetAtmosphereSceneProxy.h
│       │   └── AtmosphereWorldSubsystem.h
│       └── Private/
│           ├── PlanetAtmosphereModule.cpp
│           ├── PlanetAtmosphereActor.cpp
│           ├── PlanetAtmosphereComponent.cpp
│           ├── PlanetAtmosphereSceneProxy.cpp
│           └── AtmosphereWorldSubsystem.cpp
└── Shaders/
    └── PlanetAtmosphereCommon.ush
```

---

**Ready for Step 3 after user confirmation.**
