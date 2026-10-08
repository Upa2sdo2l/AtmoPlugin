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
		ShadowCascade0 = 9,   // Phase 4 / Step 22: cascade atlas overlays
		ShadowCascade1 = 10,
		ShadowCascade2 = 11,
		CloudShadowUsage = 12,   // Step 23: lit samples served by the cascades (green) / the fallback light march (red); Step 24: + surface
		SurfaceCloudShadow = 13, // Step 24: cloud transmittance of the sun path on the placeholder surface
		WeatherCoverage = 14,    // Phase 5 / Step 27: weather cloud water (coverage) on the cloud layer sphere
		WeatherHumidity = 15,    // Step 28: effective relative humidity
		WeatherWind = 16,        // Step 28: wind speed + direction arrows
		WeatherTemperature = 17, // Step 28: zonal temperature + isotherms
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
		int32 InterleaveFactor = 3;        // Step 19: one traced pixel per N x N block per frame (1..4)
		float StaticClampGamma = 8.0f;     // Step 19, interleaved: clamp gamma for pixels that did not move
		float ClampMotionPixels = 0.5f;    // Step 19, interleaved: motion (pixels) from which ClampGamma applies fully
	};
	FTemporalSettings GetTemporalSettings();

	/** r.PlanetAtmosphere.CloudSkyAmbientScale — global multiplier of the per-planet CloudSkyAmbientScale (Step 15), >= 0. */
	float GetCloudSkyAmbientScaleMultiplier();

	/** r.PlanetAtmosphere.LOD.* — validated (FullDetail > MinDetail > 0, fraction in [0.05, 1], light steps in [1, 16]). */
	FScreenLODSettings GetScreenLODSettings();

	/** r.PlanetAtmosphere.CloudShadows.* (Phase 4 / Step 22), validated. */
	struct FCloudShadowSettings
	{
		bool bEnabled = true;
		int32 Resolution = 512;          // texels per cascade side, multiple of 32 in [128, 1024]
		int32 GenerationSteps = 32;      // samples per texel ray through the cloud shell, [8, 128]
		int32 UpdateBudgetTiles = 32;    // 32 x 32-texel tiles generated per view and frame, [0, 768]; 0 = no updates
		double MinExtentCm = 8.0e5;      // half-size of cascade 0 near the ground (cm), from the CVar in km
		// Step 23
		bool bLighting = true;           // the cascades light the primary planet's clouds (hybrid); false = full light march
		int32 LocalMarchSteps = 3;       // samples of the short local march, [1, 8]
		double LocalMarchLengthCm = 1.0e5;   // its length (cm), from the CVar in km
		// Step 25
		float SunRebuildAngleDeg = 0.1f; // sun movement in the planet frame that starts a background rebuild, [0.01, 10]
		int32 CrossfadeFrames = 16;      // frames of the crossfade to the rebuilt set, [0, 120]
		// Step 24
		bool bSurface = true;            // cloud shadows on the placeholder surface
		bool bSurfaceMarchJitter = true; // per-pixel animated jitter of the surface march samples
		int32 SurfaceMarchSteps = 12;    // minimum samples of the march through the cloud layer from a surface point, [1, 64]
		int32 SurfaceMarchMaxSteps = 48; // maximum samples (long low-sun paths), [SurfaceMarchSteps, 128]
		float SurfaceMarchMaxStep = 0.125f;  // longest step, in units of CloudShapeScale, [0, 4]; 0 = always SurfaceMarchSteps
		float SurfaceMaxTexel = 0.0625f; // coarsest cascade texel used for the surface, in units of CloudShapeScale, [0, 16]
	};
	FCloudShadowSettings GetCloudShadowSettings();

	/** r.PlanetAtmosphere.Weather.* (Phase 5 / Step 27), validated. */
	struct FWeatherSettings
	{
		bool bEnabled = true;
		int32 Resolution = 256;                  // texels per cube-face side, multiple of 8 in [64, 512]
		int32 MaxPlanets = 4;                    // planets with weather per view (nearest first), [1, 8]
		double SnapshotIntervalSeconds = 600.0;  // game seconds between two weather snapshots, [10, 86400]
		int32 FacesPerFrame = 1;                 // cube faces of the next snapshot built per frame and planet, [1, 6]
	};
	FWeatherSettings GetWeatherSettings();

	/** r.PlanetAtmosphere.Weather.TimeScale — game seconds of weather per world second (default clock), [0, 1e6]. */
	double GetWeatherTimeScale();

	/** r.PlanetAtmosphere.Weather.TimeOffsetHours — game hours added to the weather clock (testing). */
	double GetWeatherTimeOffsetHours();
}
