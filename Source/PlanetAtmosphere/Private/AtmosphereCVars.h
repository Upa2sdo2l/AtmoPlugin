// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Console variables of the plugin (r.PlanetAtmosphere.*).
 * Accessors use GetValueOnAnyThread, so they are safe from the game and the render thread.
 */
namespace PlanetAtmosphere::CVars
{
	/** Values of r.PlanetAtmosphere.DebugMode. */
	enum class EDebugMode : int32
	{
		FinalClouds = 0,
		AtmosphereBounds = 1,
		Density = 2,
	};

	/** r.PlanetAtmosphere.Enable — master switch; 0 = nothing is rendered or dispatched. */
	bool IsEnabled();

	/** r.PlanetAtmosphere.DebugMode — see EDebugMode; unknown values fall back to FinalClouds. */
	EDebugMode GetDebugMode();

	/** r.PlanetAtmosphere.DebugIntensity — brightness multiplier of the Atmosphere Bounds overlay. */
	float GetDebugIntensity();

	/** r.PlanetAtmosphere.MaxVisible — max atmospheres per view, clamped to [1, PLANET_ATMOSPHERE_MAX_VISIBLE]. */
	int32 GetMaxVisible();

	/** r.PlanetAtmosphere.DebugPlanetSurface — draw a placeholder planet surface (for levels without terrain). */
	bool ShouldDrawPlanetSurface();

	/** r.PlanetAtmosphere.CloudAmbientIntensity — ambient (sky) light on clouds as a fraction of the sun illuminance. */
	float GetCloudAmbientIntensity();

	/** r.PlanetAtmosphere.LightSteps — samples of the light march toward the sun, clamped to [1, 16]. */
	int32 GetLightSteps();
}
