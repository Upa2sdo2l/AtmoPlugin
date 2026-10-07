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
		CloudHeight = 3,
		RaySteps = 4,
		AtmosphereOnly = 5,
		TransmittanceLut = 6,
		MultipleScatteringLut = 7,
		TemporalWeight = 8,
	};

	/** Values of r.PlanetAtmosphere.StepDistribution. */
	enum class EStepDistribution : int32
	{
		Uniform = 0,
		CameraCentered = 1,
	};

	/** Values of r.PlanetAtmosphere.Jitter. */
	enum class EJitterMode : int32
	{
		Off = 0,
		Static = 1,
		Animated = 2,
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

	// ---- Raymarch sample distribution (Phase 2 / Step 9) ----

	/** r.PlanetAtmosphere.StepDistribution — uniform or camera-centered steps. */
	EStepDistribution GetStepDistribution();

	/** r.PlanetAtmosphere.StepNearDistance — T0 of the camera-centered distribution in cloud-layer thicknesses, clamped to [0.01, 100]. */
	float GetStepNearDistance();

	/** r.PlanetAtmosphere.StepRatioMax — per-ray cap of last step / first step (camera-centered); <= 1 = no cap. */
	float GetStepRatioMax();

	/** r.PlanetAtmosphere.Jitter — per-pixel offset of the samples inside each step. */
	EJitterMode GetJitterMode();

	/** r.PlanetAtmosphere.EmptySpaceSkip — steps covered by one coarse probe in empty space; 0 = off, else clamped to [2, 8]. */
	int32 GetEmptySpaceSkipSpan();

	/** r.PlanetAtmosphere.MinTransmittance — the ray stops below this transmittance, clamped to [0, 0.2]. */
	float GetMinTransmittance();

	// ---- Noise (Phase 2 / Step 10) ----

	/** Values of r.PlanetAtmosphere.NoiseSource. */
	enum class ENoiseSource : int32
	{
		Procedural = 0,   // Phase 1 PA_Fbm: reference and fallback
		BakedTextures = 1,
	};

	/** r.PlanetAtmosphere.NoiseSource — base-shape / erosion noise source. */
	ENoiseSource GetNoiseSource();

	// ---- Screen-space LOD (Phase 2 / Step 11) ----

	struct FScreenLODSettings
	{
		bool bEnabled = true;
		float FullDetailRadiusPx = 400.0f;  // >= this radius on screen: full RaymarchSteps / LightSteps
		float MinDetailRadiusPx = 50.0f;    // <= this radius: minimum detail
		float MinStepFraction = 0.25f;      // RaymarchSteps multiplier at minimum detail
		int32 MinLightSteps = 2;            // light steps at minimum detail
	};

	/** r.PlanetAtmosphere.NoiseFootprintScale — multiplier of the pixel footprint for noise octave fading, clamped to [0.01, 1]. */
	float GetNoiseFootprintScale();

	struct FLightLODSettings
	{
		bool bEnabled = true;
		float FullDetailFootprint = 0.0625f;   // in cloud-layer thicknesses: full light steps at or below
		float MinDetailFootprint = 1.0f;       // in cloud-layer thicknesses: minimum light steps at or above
		int32 MinLightSteps = 2;
	};

	/** r.PlanetAtmosphere.LightLOD.* — light-march steps by the true pixel footprint (Step 12, variant 2). */
	FLightLODSettings GetLightLODSettings();

	// ---- Atmosphere (Phase 2.5 / Step 13) ----

	/** r.PlanetAtmosphere.Atmosphere — atmosphere single scattering on / off. */
	bool IsAtmosphereEnabled();

	/** r.PlanetAtmosphere.Atmosphere.Steps — view-ray samples of the atmosphere, clamped to [4, 64]. */
	int32 GetAtmosphereSteps();

	/** r.PlanetAtmosphere.Atmosphere.MultipleScattering — multiple-scattering LUT + term on / off (Step 14). */
	bool IsMultipleScatteringEnabled();

	/** r.PlanetAtmosphere.Atmosphere.LutCache — LUTs cached across frames (Step 16); 0 = rebuilt every frame. */
	bool IsLutCacheEnabled();

	/** r.PlanetAtmosphere.Temporal.* (Phase 3 / Step 18), validated. */
	struct FTemporalSettings
	{
		bool bEnabled = true;
		float CurrentFrameWeight = 0.1f;   // minimum weight of the new frame (history converges over ~1 / weight frames)
		float ClampGamma = 1.25f;          // history clamped to mean +- gamma x std of the current 3x3 neighbourhood
		float DepthRejectRatio = 4.0f;     // history rejected when its depth differs by more than this factor
	};
	FTemporalSettings GetTemporalSettings();

	/** r.PlanetAtmosphere.CloudSkyAmbientScale — global multiplier of the per-planet CloudSkyAmbientScale (Step 15), >= 0. */
	float GetCloudSkyAmbientScaleMultiplier();

	/** r.PlanetAtmosphere.LOD.* — validated (FullDetail > MinDetail > 0, fraction in [0.05, 1], light steps in [1, 16]). */
	FScreenLODSettings GetScreenLODSettings();
}
