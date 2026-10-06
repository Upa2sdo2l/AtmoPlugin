// Copyright Epic Games, Inc. All Rights Reserved.

#include "AtmosphereCVars.h"
#include "PlanetAtmosphereTypes.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarPlanetAtmosphereEnable(
		TEXT("r.PlanetAtmosphere.Enable"),
		1,
		TEXT("Master switch for PlanetAtmosphere rendering. 0 = off (nothing is dispatched), 1 = on."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereDebugMode(
		TEXT("r.PlanetAtmosphere.DebugMode"),
		0,
		TEXT("PlanetAtmosphere view mode.\n")
		TEXT(" 0 = Final Clouds (default)\n")
		TEXT(" 1 = Atmosphere Bounds (planet sphere, atmosphere shell, cloud shell)\n")
		TEXT(" 2 = Density (optical depth of the clouds along the view ray)"),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarPlanetAtmosphereDebugIntensity(
		TEXT("r.PlanetAtmosphere.DebugIntensity"),
		1.0f,
		TEXT("Brightness multiplier of the Atmosphere Bounds overlay (applied before tonemapping/exposure)."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereMaxVisible(
		TEXT("r.PlanetAtmosphere.MaxVisible"),
		PLANET_ATMOSPHERE_MAX_VISIBLE,
		TEXT("Maximum number of atmospheres rendered per view (closest first). Clamped to [1, 16]."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereDebugPlanetSurface(
		TEXT("r.PlanetAtmosphere.DebugPlanetSurface"),
		1,
		TEXT("Draw a dark placeholder surface on the planet sphere in Final Clouds mode, for levels without terrain.\n")
		TEXT("Scene geometry closer than the sphere always wins. 0 = off, 1 = on."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarPlanetAtmosphereCloudAmbientIntensity(
		TEXT("r.PlanetAtmosphere.CloudAmbientIntensity"),
		1.0f,
		TEXT("Ambient light on the clouds (temporary Phase 1 lighting until sun lighting arrives)."),
		ECVF_RenderThreadSafe);
}

namespace PlanetAtmosphere::CVars
{
	bool IsEnabled()
	{
		return CVarPlanetAtmosphereEnable.GetValueOnAnyThread(false) != 0;
	}

	EDebugMode GetDebugMode()
	{
		switch (CVarPlanetAtmosphereDebugMode.GetValueOnAnyThread(false))
		{
		case 1:  return EDebugMode::AtmosphereBounds;
		case 2:  return EDebugMode::Density;
		default: return EDebugMode::FinalClouds;
		}
	}

	float GetDebugIntensity()
	{
		return FMath::Max(0.0f, CVarPlanetAtmosphereDebugIntensity.GetValueOnAnyThread(false));
	}

	int32 GetMaxVisible()
	{
		return FMath::Clamp(CVarPlanetAtmosphereMaxVisible.GetValueOnAnyThread(false), 1, PLANET_ATMOSPHERE_MAX_VISIBLE);
	}

	bool ShouldDrawPlanetSurface()
	{
		return CVarPlanetAtmosphereDebugPlanetSurface.GetValueOnAnyThread(false) != 0;
	}

	float GetCloudAmbientIntensity()
	{
		return FMath::Max(0.0f, CVarPlanetAtmosphereCloudAmbientIntensity.GetValueOnAnyThread(false));
	}
}
