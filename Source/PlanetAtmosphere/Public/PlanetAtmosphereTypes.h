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
 * Validated atmosphere scattering parameters (Phase 2.5 / Step 13), render-side units:
 * coefficients in 1/cm, heights and altitudes in cm. Altitudes are measured from the ATMOSPHERE BOTTOM
 * (FPlanetAtmosphereRadii::AtmosphereBottom), which is where the density profiles start.
 * Built once by UPlanetAtmosphereComponent::GetValidatedScatteringUU(), copied by value to the render thread.
 */
struct FPlanetAtmosphereScattering
{
	FVector3f RayleighScattering = FVector3f::ZeroVector;   // 1/cm at the atmosphere bottom
	float RayleighScaleHeight = 1.0f;                       // cm, exponential profile
	FVector3f MieScattering = FVector3f::ZeroVector;        // 1/cm
	FVector3f MieAbsorption = FVector3f::ZeroVector;        // 1/cm
	float MieScaleHeight = 1.0f;                            // cm, exponential profile
	float MieAnisotropy = 0.0f;                             // g of the Cornette-Shanks phase function
	FVector3f OzoneAbsorption = FVector3f::ZeroVector;      // 1/cm at the tent peak
	float OzoneLayerAltitude = 0.0f;                        // cm, tent peak
	float OzoneLayerWidth = 1.0f;                           // cm, full width of the tent (0 at +-width/2)
	FVector3f SurfaceAlbedo = FVector3f::ZeroVector;        // placeholder surface now, ground bounce in Step 14
};

/**
 * The sun used to light all atmospheres of one world (Step 7: a single sun per level).
 * Gathered on the game thread from a Directional Light, copied to the render thread by value.
 */
struct FAtmosphereSunLight
{
	/** Unit vector, world space, pointing TOWARD the sun (= -ULightComponent::GetDirection()). */
	FVector3d DirectionToSun = FVector3d::ZAxisVector;

	/** Illuminance in lux, including light color and temperature (ULightComponent::GetColoredLightBrightness()). */
	FLinearColor Illuminance = FLinearColor::Black;

	/** False if the world has no usable Directional Light. */
	bool bValid = false;
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

	/** Sky ambient multiplier on the clouds (Step 15, temporary compensation until Phase 7). */
	float CloudSkyAmbientScale = 0.0f;

	/** Atmosphere scattering (Step 13), render-side units. */
	FPlanetAtmosphereScattering ScatteringUU;
};
