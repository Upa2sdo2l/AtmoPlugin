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
		TEXT(" 7 = Multiple-Scattering LUT (final image + the MS LUT atlas at 4x in the top-left corner, one 64 x 32 block per planet, x25)\n")
		TEXT(" 8 = Temporal Weight (weight of the current frame in the temporal accumulation: green = history, red = current frame only / history rejected)\n")
		TEXT(" 9 / 10 / 11 = Shadow Cascade 0 / 1 / 2 (final image + the cloud shadow cascade of the primary planet in the top-left corner:\n")
		TEXT("   white = lit column, dark blue = shadowed, magenta checker = not generated yet, red ring = sub-camera point; red square = no cascades)\n")
		TEXT(" 12 = Cloud Shadow Usage (lit cloud samples of the primary planet: green = lit through the cascades, red = full light march fallback;\n")
		TEXT("   pixels without such samples show the surface shadow source: green = cascades, blue = march through the layer, red = march, tiles not generated yet)\n")
		TEXT(" 13 = Surface Cloud Shadow (cloud transmittance of the sun path on the placeholder surface: white = lit, black = shadowed; dark blue = no direct sun)"),
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
		TEXT("Ambient light on the clouds as a fraction of the sun illuminance; fades out on the night side.\n")
		TEXT("Used only when the sky ambient of Step 15 is unavailable (r.PlanetAtmosphere.Atmosphere 0 or\n")
		TEXT("r.PlanetAtmosphere.Atmosphere.MultipleScattering 0). Without a Directional Light a neutral ambient of 10 lux x this value is used."),
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

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereAtmosphereLutCache(
		TEXT("r.PlanetAtmosphere.Atmosphere.LutCache"),
		1,
		TEXT("Atmosphere LUT cache across frames (Step 16). 1 = rebuild a LUT only when its atmosphere's parameters change (default),\n")
		TEXT("0 = rebuild every LUT of every view every frame (pre-Step 16 cost; for comparison, or after recompileshaders)."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereTemporal(
		TEXT("r.PlanetAtmosphere.Temporal"),
		1,
		TEXT("Temporal accumulation of the clouds + atmosphere (Phase 3 / Step 18): reprojected history of previous frames\n")
		TEXT("(camera and planet motion), clamped to the current neighbourhood. 1 = on (default), 0 = every frame on its own.\n")
		TEXT("Used in debug modes 0, 5 and 8 for views with a persistent view state (not scene captures / reflections)."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarPlanetAtmosphereTemporalCurrentFrameWeight(
		TEXT("r.PlanetAtmosphere.Temporal.CurrentFrameWeight"),
		0.1f,
		TEXT("Minimum weight of the current frame (0.01..1). Lower = smoother, more lag when things move. Default 0.1."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarPlanetAtmosphereTemporalClampGamma(
		TEXT("r.PlanetAtmosphere.Temporal.ClampGamma"),
		1.25f,
		TEXT("History is clamped to mean +- ClampGamma x std of the current 3x3 neighbourhood (0.25..8). Lower = less ghosting,\n")
		TEXT("more noise. Default 1.25."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarPlanetAtmosphereTemporalDepthRejectRatio(
		TEXT("r.PlanetAtmosphere.Temporal.DepthRejectRatio"),
		4.0f,
		TEXT("History is dropped where its depth differs from the reprojected depth by more than this factor (1.1..100):\n")
		TEXT("disocclusion and camera jumps without a camera cut. Default 4."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereTemporalInterleave(
		TEXT("r.PlanetAtmosphere.Temporal.Interleave"),
		3,
		TEXT("Interleaved rendering (Phase 3 / Step 19): the raymarch traces one pixel per N x N block per frame, the temporal\n")
		TEXT("pass reconstructs the rest. 1 = every pixel, 2 = 2x2, 3 = 3x3 (default, ~1/9 of the raymarch cost), 4 = 4x4.\n")
		TEXT("Only with r.PlanetAtmosphere.Temporal 1 (views without temporal always trace every pixel)."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarPlanetAtmosphereTemporalStaticClampGamma(
		TEXT("r.PlanetAtmosphere.Temporal.StaticClampGamma"),
		8.0f,
		TEXT("Interleaved (Step 19): clamp gamma for pixels that did not move (0.25..1000). The clamp neighbourhood is made of\n")
		TEXT("samples N pixels apart; the normal gamma would keep a static image from converging. Default 8."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarPlanetAtmosphereTemporalClampMotionPixels(
		TEXT("r.PlanetAtmosphere.Temporal.ClampMotionPixels"),
		0.5f,
		TEXT("Interleaved (Step 19): motion in pixels from which r.PlanetAtmosphere.Temporal.ClampGamma applies fully (0.01..16).\n")
		TEXT("Default 0.5."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarPlanetAtmosphereCloudSkyAmbientScale(
		TEXT("r.PlanetAtmosphere.CloudSkyAmbientScale"),
		1.0f,
		TEXT("Global multiplier of the per-planet Cloud Sky Ambient Scale (Details panel, default 5) - Step 15.\n")
		TEXT("Cloud ambient = sky radiance from the multiple-scattering LUT x Cloud Sky Ambient Scale x this.\n")
		TEXT("The per-planet value is a TEMPORARY compensation for the missing in-cloud multiple scattering (Phase 7);\n")
		TEXT("0.2 x the default 5 = the physically correct x1 (clouds much darker). Default 1."),
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

	// ---- Cloud shadow cascades (Phase 4 / Step 22) ----

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereCloudShadows(
		TEXT("r.PlanetAtmosphere.CloudShadows"),
		1,
		TEXT("Cloud shadow cascades of the primary planet of each view (Phase 4): 3 sun-aligned Beer shadow maps around the\n")
		TEXT("sub-camera point, generated progressively and kept across frames. Step 22: generated and shown in debug modes 9-11\n")
		TEXT("only (the image does not use them yet). 1 = on (default), 0 = off (nothing is generated)."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereCloudShadowsResolution(
		TEXT("r.PlanetAtmosphere.CloudShadows.Resolution"),
		512,
		TEXT("Texels per cascade side; the atlas is Res x 3 Res RGBA16F (512: 6 MB per view). Rounded down to a multiple of 32,\n")
		TEXT("clamped to [128, 1024]. A change regenerates every cascade."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereCloudShadowsGenerationSteps(
		TEXT("r.PlanetAtmosphere.CloudShadows.GenerationSteps"),
		32,
		TEXT("Density samples per cascade texel along the sun through the cloud shell (Step 21 prototype: 16 -> +0.4 pt error).\n")
		TEXT("Clamped to [8, 128]. A change regenerates every cascade."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereCloudShadowsUpdateBudget(
		TEXT("r.PlanetAtmosphere.CloudShadows.UpdateBudget"),
		32,
		TEXT("Tiles of 32 x 32 cascade texels generated per view and frame (the GPU cost of the cascades, stat gpu ->\n")
		TEXT("PlanetAtmosphere.CloudShadows). New tiles first near the camera, round-robin over the 3 cascades. A full set of\n")
		TEXT("3 cascades at 512 = 768 tiles. Measured on an RTX 3050 (frame peaks): 8 -> 0.11 ms, 16 -> 0.14 ms, 32 -> 0.23 ms;\n")
		TEXT("32 (default) fills all 3 cascades in 24 frames. 0 = no generation (the cascades stay as they are). Clamped to [0, 768]."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarPlanetAtmosphereCloudShadowsMinExtent(
		TEXT("r.PlanetAtmosphere.CloudShadows.MinExtent"),
		8.0f,
		TEXT("Half-size of cascade 0 near the cloud layer, in km. Higher up it grows with the camera height above the clouds\n")
		TEXT("in powers of two; cascade i = cascade 0 x 4^i. Clamped to [1, 1000]."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereCloudShadowsLighting(
		TEXT("r.PlanetAtmosphere.CloudShadows.Lighting"),
		1,
		TEXT("Phase 4 / Step 23: sunlight on the primary planet's clouds = short local march toward the sun (LocalMarchSteps over\n")
		TEXT("LocalMarchLength) x the cloud shadow cascades for the rest of the sun path; where no cascade texel is available the\n")
		TEXT("full light march (r.PlanetAtmosphere.LightSteps) is used. 1 = on (default), 0 = full light march everywhere (A/B)."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereCloudShadowsLocalMarchSteps(
		TEXT("r.PlanetAtmosphere.CloudShadows.LocalMarchSteps"),
		3,
		TEXT("Samples of the short local march from each cloud sample toward the sun (fine self-shadowing, cloud edges).\n")
		TEXT("1 = fast, 2 = compromise, 3 = default, 4+ = higher quality. Clamped to [1, 8]. Reduced for distant samples by\n")
		TEXT("r.PlanetAtmosphere.LightLOD like the full light march (down to LightLOD.MinLightSteps)."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarPlanetAtmosphereCloudShadowsLocalMarchLength(
		TEXT("r.PlanetAtmosphere.CloudShadows.LocalMarchLength"),
		1.0f,
		TEXT("Length of the local march in km (Step 21 prototype: 1 km; longer with few steps is worse). Clamped to [0.1, 20]."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereCloudShadowsSurface(
		TEXT("r.PlanetAtmosphere.CloudShadows.Surface"),
		1,
		TEXT("Phase 4 / Step 24: shadows of the clouds on the direct sunlight of the placeholder planet surface\n")
		TEXT("(r.PlanetAtmosphere.DebugPlanetSurface). Primary planet: the cascades where their texel is fine enough\n")
		TEXT("(SurfaceMaxTexel), a march through the cloud layer elsewhere; other planets: the march. Scene geometry is not shadowed.\n")
		TEXT("1 = on (default), 0 = off (A/B)."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPlanetAtmosphereCloudShadowsSurfaceMarchSteps(
		TEXT("r.PlanetAtmosphere.CloudShadows.SurfaceMarchSteps"),
		12,
		TEXT("Samples of the march from a surface point through the whole cloud layer toward the sun (uniform; used where the\n")
		TEXT("cascades are too coarse, e.g. from orbit, and for planets without cascades). Step 24 prototype (mean error x 100,\n")
		TEXT("worst scene): 8 -> 7.4, 12 -> 3.5 (default), 16 -> 2.4. Clamped to [1, 64]."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarPlanetAtmosphereCloudShadowsSurfaceMaxTexel(
		TEXT("r.PlanetAtmosphere.CloudShadows.SurfaceMaxTexel"),
		0.0625f,
		TEXT("Coarsest cascade texel used for surface shadows, in units of the planet's CloudShapeScale (0.0625 = 1/16, i.e.\n")
		TEXT("500 m at the default 8 km); surface points whose cascade is coarser are marched (SurfaceMarchSteps). 0 = always\n")
		TEXT("march. Clamped to [0, 16]."),
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
		case 8:  return EDebugMode::TemporalWeight;
		case 9:  return EDebugMode::ShadowCascade0;
		case 10: return EDebugMode::ShadowCascade1;
		case 11: return EDebugMode::ShadowCascade2;
		case 12: return EDebugMode::CloudShadowUsage;
		case 13: return EDebugMode::SurfaceCloudShadow;
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

	bool IsLutCacheEnabled()
	{
		return CVarPlanetAtmosphereAtmosphereLutCache.GetValueOnAnyThread(false) != 0;
	}

	FTemporalSettings GetTemporalSettings()
	{
		FTemporalSettings Settings;
		Settings.bEnabled = CVarPlanetAtmosphereTemporal.GetValueOnAnyThread(false) != 0;
		Settings.CurrentFrameWeight = FMath::Clamp(CVarPlanetAtmosphereTemporalCurrentFrameWeight.GetValueOnAnyThread(false), 0.01f, 1.0f);
		Settings.ClampGamma = FMath::Clamp(CVarPlanetAtmosphereTemporalClampGamma.GetValueOnAnyThread(false), 0.25f, 8.0f);
		Settings.DepthRejectRatio = FMath::Clamp(CVarPlanetAtmosphereTemporalDepthRejectRatio.GetValueOnAnyThread(false), 1.1f, 100.0f);
		Settings.InterleaveFactor = FMath::Clamp(CVarPlanetAtmosphereTemporalInterleave.GetValueOnAnyThread(false), 1, 4);
		Settings.StaticClampGamma = FMath::Clamp(CVarPlanetAtmosphereTemporalStaticClampGamma.GetValueOnAnyThread(false), 0.25f, 1000.0f);
		Settings.ClampMotionPixels = FMath::Clamp(CVarPlanetAtmosphereTemporalClampMotionPixels.GetValueOnAnyThread(false), 0.01f, 16.0f);
		return Settings;
	}

	float GetCloudSkyAmbientScaleMultiplier()
	{
		return FMath::Max(0.0f, CVarPlanetAtmosphereCloudSkyAmbientScale.GetValueOnAnyThread(false));
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

	FCloudShadowSettings GetCloudShadowSettings()
	{
		FCloudShadowSettings Settings;
		Settings.bEnabled = CVarPlanetAtmosphereCloudShadows.GetValueOnAnyThread(false) != 0;
		Settings.Resolution = FMath::Clamp(CVarPlanetAtmosphereCloudShadowsResolution.GetValueOnAnyThread(false) / 32 * 32, 128, 1024);
		Settings.GenerationSteps = FMath::Clamp(CVarPlanetAtmosphereCloudShadowsGenerationSteps.GetValueOnAnyThread(false), 8, 128);
		Settings.UpdateBudgetTiles = FMath::Clamp(CVarPlanetAtmosphereCloudShadowsUpdateBudget.GetValueOnAnyThread(false), 0, 768);
		Settings.MinExtentCm = FMath::Clamp(static_cast<double>(CVarPlanetAtmosphereCloudShadowsMinExtent.GetValueOnAnyThread(false)), 1.0, 1000.0) * 1.0e5;
		Settings.bLighting = CVarPlanetAtmosphereCloudShadowsLighting.GetValueOnAnyThread(false) != 0;
		Settings.LocalMarchSteps = FMath::Clamp(CVarPlanetAtmosphereCloudShadowsLocalMarchSteps.GetValueOnAnyThread(false), 1, 8);
		Settings.LocalMarchLengthCm = FMath::Clamp(static_cast<double>(CVarPlanetAtmosphereCloudShadowsLocalMarchLength.GetValueOnAnyThread(false)), 0.1, 20.0) * 1.0e5;
		Settings.bSurface = CVarPlanetAtmosphereCloudShadowsSurface.GetValueOnAnyThread(false) != 0;
		Settings.SurfaceMarchSteps = FMath::Clamp(CVarPlanetAtmosphereCloudShadowsSurfaceMarchSteps.GetValueOnAnyThread(false), 1, 64);
		Settings.SurfaceMaxTexel = FMath::Clamp(CVarPlanetAtmosphereCloudShadowsSurfaceMaxTexel.GetValueOnAnyThread(false), 0.0f, 16.0f);
		return Settings;
	}
}
