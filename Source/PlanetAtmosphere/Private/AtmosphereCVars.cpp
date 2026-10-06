// Copyright Epic Games, Inc. All Rights Reserved.

#include "AtmosphereCVars.h"
#include "PlanetAtmosphereTypes.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarPlanetAtmosphereEnable(
		TEXT("r.PlanetAtmosphere.Enable"),
		1,
		TEXT("Master switch for PlanetAtmosphere rendering. 0 = off, 1 = on."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereDebugMode(
		TEXT("r.PlanetAtmosphere.DebugMode"),
		1,
		TEXT("PlanetAtmosphere debug visualization.\n")
		TEXT(" 0 = off\n")
		TEXT(" 1 = Atmosphere Bounds (planet sphere, atmosphere shell, cloud shell) (default during Phase 1)"),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarPlanetAtmosphereDebugIntensity(
		TEXT("r.PlanetAtmosphere.DebugIntensity"),
		1.0f,
		TEXT("Brightness multiplier of PlanetAtmosphere debug overlays (applied before tonemapping/exposure)."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereMaxVisible(
		TEXT("r.PlanetAtmosphere.MaxVisible"),
		PLANET_ATMOSPHERE_MAX_VISIBLE,
		TEXT("Maximum number of atmospheres rendered per view (closest first). Clamped to [1, 16]."),
		ECVF_RenderThreadSafe);
}

namespace PlanetAtmosphere::CVars
{
	bool IsEnabled()
	{
		return CVarPlanetAtmosphereEnable.GetValueOnAnyThread(false) != 0;
	}

	int32 GetDebugMode()
	{
		return CVarPlanetAtmosphereDebugMode.GetValueOnAnyThread(false);
	}

	float GetDebugIntensity()
	{
		return FMath::Max(0.0f, CVarPlanetAtmosphereDebugIntensity.GetValueOnAnyThread(false));
	}

	int32 GetMaxVisible()
	{
		return FMath::Clamp(CVarPlanetAtmosphereMaxVisible.GetValueOnAnyThread(false), 1, PLANET_ATMOSPHERE_MAX_VISIBLE);
	}
}
