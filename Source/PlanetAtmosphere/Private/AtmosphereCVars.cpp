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
		TEXT(" 0 = Final (clouds + atmosphere, default)\n")
		TEXT(" 1 = Atmosphere Bounds (planet sphere, atmosphere shell, cloud shell)\n")
		TEXT(" 2 = Density (optical depth of the clouds along the view ray)\n")
		TEXT(" 3 = Cloud Height (where inside the cloud layer the visible clouds are: blue = bottom, red = top)\n")
		TEXT(" 4 = Ray Steps (density-function calls per pixel incl. light march; white = 64 x (1 + LightSteps))\n")
		TEXT(" 5 = Atmosphere Only (final image without clouds)\n")
		TEXT(" 6 = Transmittance LUT (final image + the LUT atlas at 2x in the top-left corner, one 256 x 64 block per planet)\n")
		TEXT(" 7 = Multiple-Scattering LUT (final image + the MS LUT atlas at 4x in the top-left corner, one 64 x 32 block per planet, x25)"),
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
		0.1f,
		TEXT("Ambient (sky) light on the clouds as a fraction of the sun illuminance; fades out on the night side.\n")
		TEXT("Without a Directional Light a neutral ambient of 10 lux x this value is used."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereLightSteps(
		TEXT("r.PlanetAtmosphere.LightSteps"),
		6,
		TEXT("Samples of the light march toward the sun per cloud sample (self-shadowing). Clamped to [1, 16]."),
		ECVF_RenderThreadSafe);

	// ---- Raymarch sample distribution (Phase 2 / Step 9) ----

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereStepDistribution(
		TEXT("r.PlanetAtmosphere.StepDistribution"),
		1,
		TEXT("Distribution of the Raymarch Steps along the view ray inside the cloud layer.\n")
		TEXT(" 0 = Uniform (Phase 1 behaviour)\n")
		TEXT(" 1 = Camera-centered (default): small steps near the camera, larger far away (see StepNearDistance, StepRatioMax)"),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarPlanetAtmosphereStepNearDistance(
		TEXT("r.PlanetAtmosphere.StepNearDistance"),
		1.0f,
		TEXT("Camera-centered steps grow proportionally to (distance + StepNearDistance x cloud layer thickness).\n")
		TEXT("Smaller = finer steps right at the camera. Clamped to [0.01, 100]."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarPlanetAtmosphereStepRatioMax(
		TEXT("r.PlanetAtmosphere.StepRatioMax"),
		4.0f,
		TEXT("Camera-centered: maximum ratio of the last step to the first step of one ray (protects far clouds).\n")
		TEXT("<= 1 disables the cap. Default 4."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereJitter(
		TEXT("r.PlanetAtmosphere.Jitter"),
		2,
		TEXT("Per-pixel offset of the raymarch samples inside each step (interleaved gradient noise).\n")
		TEXT(" 0 = Off (step centers; banding)\n")
		TEXT(" 1 = Static pattern\n")
		TEXT(" 2 = Animated every frame (default; averaged by TSR/TAA while the camera is still)"),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereEmptySpaceSkip(
		TEXT("r.PlanetAtmosphere.EmptySpaceSkip"),
		0,
		TEXT("Empty-space skipping: number of steps covered by one coarse probe while the ray is in clear air.\n")
		TEXT("0 = off (default: the CPU prototype showed lost thin cloud structures). 2..8 = on."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarPlanetAtmosphereMinTransmittance(
		TEXT("r.PlanetAtmosphere.MinTransmittance"),
		0.01f,
		TEXT("The view ray stops when the remaining transmittance drops below this (early exit). Clamped to [0, 0.2]."),
		ECVF_RenderThreadSafe);

	// ---- Noise (Phase 2 / Step 10) ----

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereNoiseSource(
		TEXT("r.PlanetAtmosphere.NoiseSource"),
		1,
		TEXT("Source of the cloud base-shape and erosion noise (the weather mask is always procedural).\n")
		TEXT(" 0 = Procedural (Phase 1 noise, reference and fallback)\n")
		TEXT(" 1 = Baked tileable 3D textures (default): 128^3 + 64^3 R16F, baked once on the GPU"),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarPlanetAtmosphereNoiseFootprintScale(
		TEXT("r.PlanetAtmosphere.NoiseFootprintScale"),
		0.25f,
		TEXT("Noise octaves fade at this fraction of the pixel footprint (density LOD, Step 12).\n")
		TEXT("< 1: sub-pixel cloud detail is point-sampled and averaged over frames by TSR / temporal accumulation,\n")
		TEXT("so distant planets keep their cloud cover (unbiased). Costs a little more far away and shimmers more while\n")
		TEXT("the camera moves (until Phase 3 temporal). 1 = Phase 1 behaviour (clouds of distant planets fade out). Clamped to [0.01, 1]."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereLightLOD(
		TEXT("r.PlanetAtmosphere.LightLOD"),
		1,
		TEXT("Light-march LOD (Step 12): fewer light steps for cloud samples whose pixel is large compared to the cloud layer.\n")
		TEXT("0 = off (always LightSteps), 1 = on."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarPlanetAtmosphereLightLODFullFootprint(
		TEXT("r.PlanetAtmosphere.LightLOD.FullDetailFootprint"),
		0.0625f,
		TEXT("Pixel footprint (in cloud-layer thicknesses) at or below which the full LightSteps are used."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarPlanetAtmosphereLightLODMinFootprint(
		TEXT("r.PlanetAtmosphere.LightLOD.MinDetailFootprint"),
		1.0f,
		TEXT("Pixel footprint (in cloud-layer thicknesses) at or above which LightLOD.MinLightSteps are used (log2 interpolation in between)."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereLightLODMinSteps(
		TEXT("r.PlanetAtmosphere.LightLOD.MinLightSteps"),
		2,
		TEXT("Light steps for samples with a large pixel footprint. Clamped to [1, 16] and never above the planet's light steps."),
		ECVF_RenderThreadSafe);

	// ---- Atmosphere (Phase 2.5 / Step 13) ----

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereAtmosphere(
		TEXT("r.PlanetAtmosphere.Atmosphere"),
		1,
		TEXT("Atmosphere single scattering (Rayleigh + Mie + ozone; parameters on the PlanetAtmosphereComponent).\n")
		TEXT("0 = off (clouds only, Phase 2 image), 1 = on (default). The transmittance LUT pass runs in both cases (a few microseconds)."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereAtmosphereSteps(
		TEXT("r.PlanetAtmosphere.Atmosphere.Steps"),
		16,
		TEXT("Samples of the atmosphere along each view ray (quadratic distribution from inside the atmosphere, uniform from outside).\n")
		TEXT("Default 16 (CPU prototype: mean luminance error <= 0.9 %). Clamped to [4, 64]."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereAtmosphereMultipleScattering(
		TEXT("r.PlanetAtmosphere.Atmosphere.MultipleScattering"),
		1,
		TEXT("Multiple scattering of the atmosphere (Step 14, Hillaire 2020 LUT, includes ground bounce with SurfaceAlbedo).\n")
		TEXT("0 = single scattering only (Step 13 image; the MS LUT pass is skipped), 1 = on (default)."),
		ECVF_RenderThreadSafe);

	// ---- Screen-space LOD (Phase 2 / Step 11) ----

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereLOD(
		TEXT("r.PlanetAtmosphere.LOD"),
		1,
		TEXT("Screen-space LOD: fewer raymarch / light steps for atmospheres that are small on screen. 0 = off, 1 = on."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarPlanetAtmosphereLODFullDetailRadius(
		TEXT("r.PlanetAtmosphere.LOD.FullDetailRadius"),
		400.0f,
		TEXT("Radius of the atmosphere on screen (pixels of the render resolution) from which full detail is used."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarPlanetAtmosphereLODMinDetailRadius(
		TEXT("r.PlanetAtmosphere.LOD.MinDetailRadius"),
		50.0f,
		TEXT("Radius on screen (pixels) at and below which the minimum detail is used. Detail is interpolated in log2(radius) in between."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarPlanetAtmosphereLODMinStepFraction(
		TEXT("r.PlanetAtmosphere.LOD.MinStepFraction"),
		0.25f,
		TEXT("Fraction of the planet's Raymarch Steps used at minimum detail (at least 4 steps). Clamped to [0.05, 1]."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereLODMinLightSteps(
		TEXT("r.PlanetAtmosphere.LOD.MinLightSteps"),
		2,
		TEXT("Light-march steps at minimum detail (never more than r.PlanetAtmosphere.LightSteps). Clamped to [1, 16]."),
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
		case 3:  return EDebugMode::CloudHeight;
		case 4:  return EDebugMode::RaySteps;
		case 5:  return EDebugMode::AtmosphereOnly;
		case 6:  return EDebugMode::TransmittanceLut;
		case 7:  return EDebugMode::MultipleScatteringLut;
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

	int32 GetLightSteps()
	{
		return FMath::Clamp(CVarPlanetAtmosphereLightSteps.GetValueOnAnyThread(false), 1, 16);
	}

	EStepDistribution GetStepDistribution()
	{
		return CVarPlanetAtmosphereStepDistribution.GetValueOnAnyThread(false) == 0
			? EStepDistribution::Uniform
			: EStepDistribution::CameraCentered;
	}

	float GetStepNearDistance()
	{
		return FMath::Clamp(CVarPlanetAtmosphereStepNearDistance.GetValueOnAnyThread(false), 0.01f, 100.0f);
	}

	float GetStepRatioMax()
	{
		return CVarPlanetAtmosphereStepRatioMax.GetValueOnAnyThread(false);
	}

	EJitterMode GetJitterMode()
	{
		switch (CVarPlanetAtmosphereJitter.GetValueOnAnyThread(false))
		{
		case 0:  return EJitterMode::Off;
		case 1:  return EJitterMode::Static;
		default: return EJitterMode::Animated;
		}
	}

	int32 GetEmptySpaceSkipSpan()
	{
		const int32 Span = CVarPlanetAtmosphereEmptySpaceSkip.GetValueOnAnyThread(false);
		return Span <= 0 ? 0 : FMath::Clamp(Span, 2, 8);
	}

	float GetMinTransmittance()
	{
		return FMath::Clamp(CVarPlanetAtmosphereMinTransmittance.GetValueOnAnyThread(false), 0.0f, 0.2f);
	}

	ENoiseSource GetNoiseSource()
	{
		return CVarPlanetAtmosphereNoiseSource.GetValueOnAnyThread(false) == 0
			? ENoiseSource::Procedural
			: ENoiseSource::BakedTextures;
	}

	float GetNoiseFootprintScale()
	{
		return FMath::Clamp(CVarPlanetAtmosphereNoiseFootprintScale.GetValueOnAnyThread(false), 0.01f, 1.0f);
	}

	FLightLODSettings GetLightLODSettings()
	{
		FLightLODSettings Settings;
		Settings.bEnabled = CVarPlanetAtmosphereLightLOD.GetValueOnAnyThread(false) != 0;
		Settings.FullDetailFootprint = FMath::Max(1e-4f, CVarPlanetAtmosphereLightLODFullFootprint.GetValueOnAnyThread(false));
		Settings.MinDetailFootprint = FMath::Max(Settings.FullDetailFootprint * 1.01f, CVarPlanetAtmosphereLightLODMinFootprint.GetValueOnAnyThread(false));
		Settings.MinLightSteps = FMath::Clamp(CVarPlanetAtmosphereLightLODMinSteps.GetValueOnAnyThread(false), 1, 16);
		return Settings;
	}

	bool IsAtmosphereEnabled()
	{
		return CVarPlanetAtmosphereAtmosphere.GetValueOnAnyThread(false) != 0;
	}

	int32 GetAtmosphereSteps()
	{
		return FMath::Clamp(CVarPlanetAtmosphereAtmosphereSteps.GetValueOnAnyThread(false), 4, 64);
	}

	bool IsMultipleScatteringEnabled()
	{
		return CVarPlanetAtmosphereAtmosphereMultipleScattering.GetValueOnAnyThread(false) != 0;
	}

	FScreenLODSettings GetScreenLODSettings()
	{
		FScreenLODSettings Settings;
		Settings.bEnabled = CVarPlanetAtmosphereLOD.GetValueOnAnyThread(false) != 0;
		Settings.MinDetailRadiusPx = FMath::Max(1.0f, CVarPlanetAtmosphereLODMinDetailRadius.GetValueOnAnyThread(false));
		Settings.FullDetailRadiusPx = FMath::Max(Settings.MinDetailRadiusPx * 1.01f, CVarPlanetAtmosphereLODFullDetailRadius.GetValueOnAnyThread(false));
		Settings.MinStepFraction = FMath::Clamp(CVarPlanetAtmosphereLODMinStepFraction.GetValueOnAnyThread(false), 0.05f, 1.0f);
		Settings.MinLightSteps = FMath::Clamp(CVarPlanetAtmosphereLODMinLightSteps.GetValueOnAnyThread(false), 1, 16);
		return Settings;
	}
}
