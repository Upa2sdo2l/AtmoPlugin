// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Plugin log category. Per-frame output uses Verbose: `log LogPlanetAtmosphere Verbose`. */
PLANETATMOSPHERE_API DECLARE_LOG_CATEGORY_EXTERN(LogPlanetAtmosphere, Log, All);

namespace PlanetAtmosphere
{
	/**
	 * User-facing parameters (UPlanetAtmosphereComponent) are in METERS.
	 * Everything render-side (bounds, proxy, culling, future GPU data) is in Unreal Units (cm).
	 * The conversion happens exactly once: in the component when computing bounds / building the proxy.
	 */
	inline constexpr double MetersToUnrealUnits = 100.0;
}

/**
 * Validated atmosphere shell radii.
 * Invariant: Planet <= AtmosphereBottom < AtmosphereTop, AtmosphereBottom <= CloudBottom <= CloudTop <= AtmosphereTop.
 */
struct FPlanetAtmosphereRadii
{
	double Planet = 0.0;
	double AtmosphereBottom = 0.0;
	double AtmosphereTop = 0.0;
	double CloudBottom = 0.0;
	double CloudTop = 0.0;

	FPlanetAtmosphereRadii Scaled(double Scale) const
	{
		return { Planet * Scale, AtmosphereBottom * Scale, AtmosphereTop * Scale, CloudBottom * Scale, CloudTop * Scale };
	}
};

/**
 * POD snapshot of one visible atmosphere for one view, produced on the render side by
 * FAtmosphereProxyRegistry::GatherVisibleInstances(). Contains copies only (no proxy pointers),
 * so it can safely outlive the scene proxy (e.g. be captured into future RDG passes).
 * All distances are in Unreal Units (cm), world space.
 */
struct FAtmosphereVisibleInstance
{
	FVector3d PlanetCenterWorld = FVector3d::ZeroVector;
	FPlanetAtmosphereRadii RadiiUU;
	float CloudCoverage = 0.0f;
	float CloudDensity = 0.0f;
	int32 RaymarchSteps = 0;
};
