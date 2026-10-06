// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Console variables of the plugin (r.PlanetAtmosphere.*).
 * Accessors use GetValueOnAnyThread, so they are safe from the game and the render thread.
 */
namespace PlanetAtmosphere::CVars
{
	/** r.PlanetAtmosphere.Enable — master switch for all plugin rendering. */
	bool IsEnabled();

	/** r.PlanetAtmosphere.DebugMode — 0 = off, 1 = Atmosphere Bounds. */
	int32 GetDebugMode();

	/** r.PlanetAtmosphere.DebugIntensity — brightness multiplier of debug overlays (HDR, before tonemapping). */
	float GetDebugIntensity();

	/** r.PlanetAtmosphere.MaxVisible — max atmospheres per view, clamped to [1, PLANET_ATMOSPHERE_MAX_VISIBLE]. */
	int32 GetMaxVisible();
}
