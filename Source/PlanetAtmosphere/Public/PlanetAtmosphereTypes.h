// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Plugin log category. Per-frame output uses Verbose: `log LogPlanetAtmosphere Verbose`. */
PLANETATMOSPHERE_API DECLARE_LOG_CATEGORY_EXTERN(LogPlanetAtmosphere, Log, All);

/**
 * Maximum number of atmospheres rendered per view (closest ones win).
 * Must match PA_MAX_ATMOSPHERES in Shaders/Private/PlanetAtmosphereCommon.ush.
 * A #define (not constexpr) because it is used inside SHADER_PARAMETER_ARRAY declarations.
 */
#define PLANET_ATMOSPHERE_MAX_VISIBLE 16

namespace PlanetAtmosphere
{
	/**
	 * User-facing parameters (UPlanetAtmosphereComponent) are in METERS.
	 * Everything render-side (bounds, proxy, culling, future GPU data) is in Unreal Units (cm).
	 * The conversion happens exactly once: in the component when computing bounds / building the proxy.
	 */
	inline constexpr double MetersToUnrealUnits = 100.0;

	/**
	 * Cloud extinction coefficient at normalized density 1 and CloudDensity = 1, in 1/m
	 * (typical cumulus: 0.02..0.1 1/m). Final sigma_t = Density(x) * CloudDensity * this.
	 */
	inline constexpr double BaseCloudExtinctionPerMeter = 0.04;
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

	/** Planet local axes in world space (unit length, scale removed). Clouds are evaluated in this frame. */
	FVector3d PlanetAxisX = FVector3d::XAxisVector;
	FVector3d PlanetAxisY = FVector3d::YAxisVector;
	FVector3d PlanetAxisZ = FVector3d::ZAxisVector;

	FPlanetAtmosphereRadii RadiiUU;
	float CloudCoverage = 0.0f;
	float CloudDensity = 0.0f;
	double CloudShapeScaleUU = 0.0;
	float CloudErosion = 0.0f;
	int32 RaymarchSteps = 0;
};
