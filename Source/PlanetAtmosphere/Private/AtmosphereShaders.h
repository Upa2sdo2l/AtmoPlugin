// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphResources.h"
#include "SceneView.h"
#include "PlanetAtmosphereTypes.h"

/**
 * Parameters shared by all PlanetAtmosphere passes. Included into each pass with
 * SHADER_PARAMETER_STRUCT_INCLUDE (members bound without prefix). HLSL declarations:
 * Shaders/Private/PlanetAtmosphereShaderData.ush — names and layouts must match.
 */
BEGIN_SHADER_PARAMETER_STRUCT(FAtmosphereViewParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
	SHADER_PARAMETER(FMatrix44f, ClipToTranslatedWorld)
	SHADER_PARAMETER(FVector3f, CameraTranslatedWorld)
	SHADER_PARAMETER(FVector4f, ViewRectMinAndSize)
END_SHADER_PARAMETER_STRUCT()

/**
 * Per-atmosphere data, sorted near -> far, at most PLANET_ATMOSPHERE_MAX_VISIBLE entries.
 * All distances in cm. The camera position relative to each planet and every altitude are computed
 * on the CPU in double precision (see the precision note in PlanetAtmosphereCommon.ush).
 */
BEGIN_SHADER_PARAMETER_STRUCT(FAtmosphereInstanceParameters, )
	SHADER_PARAMETER(int32, NumAtmospheres)
	// xyz = camera - planet center (world axes), w = planet radius
	SHADER_PARAMETER_ARRAY(FVector4f, AtmosphereData0, [PLANET_ATMOSPHERE_MAX_VISIBLE])
	// x = camera altitude, y = atmosphere bottom, z = atmosphere top, w = cloud bottom (altitudes above planet radius)
	SHADER_PARAMETER_ARRAY(FVector4f, AtmosphereData1, [PLANET_ATMOSPHERE_MAX_VISIBLE])
	// x = cloud top altitude, y = coverage, z = extinction at density 1 (1/cm), w = shape scale (cm)
	SHADER_PARAMETER_ARRAY(FVector4f, AtmosphereData2, [PLANET_ATMOSPHERE_MAX_VISIBLE])
	// x = erosion, y = raymarch steps, z = light steps (both after screen-space LOD, Step 11), w = cloud sky ambient scale (Step 15)
	SHADER_PARAMETER_ARRAY(FVector4f, AtmosphereData3, [PLANET_ATMOSPHERE_MAX_VISIBLE])
	// Planet local axes in world space (unit); clouds are evaluated in this frame.
	SHADER_PARAMETER_ARRAY(FVector4f, AtmosphereAxisX, [PLANET_ATMOSPHERE_MAX_VISIBLE])
	SHADER_PARAMETER_ARRAY(FVector4f, AtmosphereAxisY, [PLANET_ATMOSPHERE_MAX_VISIBLE])
	SHADER_PARAMETER_ARRAY(FVector4f, AtmosphereAxisZ, [PLANET_ATMOSPHERE_MAX_VISIBLE])
	// Atmosphere scattering (Step 13). Coefficients in 1/cm, heights / altitudes in cm above the atmosphere bottom.
	// xyz = Rayleigh scattering, w = Rayleigh scale height
	SHADER_PARAMETER_ARRAY(FVector4f, AtmosphereRayleigh, [PLANET_ATMOSPHERE_MAX_VISIBLE])
	// xyz = Mie scattering, w = Mie scale height
	SHADER_PARAMETER_ARRAY(FVector4f, AtmosphereMieScattering, [PLANET_ATMOSPHERE_MAX_VISIBLE])
	// xyz = Mie absorption, w = Mie anisotropy g
	SHADER_PARAMETER_ARRAY(FVector4f, AtmosphereMieAbsorption, [PLANET_ATMOSPHERE_MAX_VISIBLE])
	// xyz = ozone absorption at the peak, w = peak altitude
	SHADER_PARAMETER_ARRAY(FVector4f, AtmosphereOzone, [PLANET_ATMOSPHERE_MAX_VISIBLE])
	// xyz = surface albedo, w = ozone layer full width
	SHADER_PARAMETER_ARRAY(FVector4f, AtmosphereSurface, [PLANET_ATMOSPHERE_MAX_VISIBLE])
	// x = slot of this atmosphere in the LUT pools (Step 16, AtmosphereLutCache.h), yzw unused
	SHADER_PARAMETER_ARRAY(FVector4f, AtmosphereLutInfo, [PLANET_ATMOSPHERE_MAX_VISIBLE])
	// Weather (Phase 5 / Step 27, AtmosphereWeather.h): x = planet slot in the weather atlas (-1 = no weather), y / z = first
	// atlas row of the two snapshots around the weather time, w = interpolation weight of the second
	SHADER_PARAMETER_ARRAY(FVector4f, AtmosphereWeatherInfo, [PLANET_ATMOSPHERE_MAX_VISIBLE])
	// Weather climate (Step 28): x = mean temperature K, y = equator - pole difference K, z = thermal equator (rad) at the
	// current weather time, w unused (zonal temperature for DebugMode 17)
	SHADER_PARAMETER_ARRAY(FVector4f, AtmosphereWeatherClimate, [PLANET_ATMOSPHERE_MAX_VISIBLE])
END_SHADER_PARAMETER_STRUCT()

/**
 * The weather atlas (Phase 5, WeatherAtlas.ush), bound to every pass that evaluates the cloud density (Step 29: the cloud
 * water sets the local coverage) and to the raymarch's weather debug views. Without weather this frame the transmittance
 * LUT pool is bound (never sampled: AtmosphereWeatherInfo.x = -1 everywhere).
 */
BEGIN_SHADER_PARAMETER_STRUCT(FAtmosphereWeatherAtlasParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, WeatherAtlas)
	SHADER_PARAMETER_SAMPLER(SamplerState, WeatherSampler)
	SHADER_PARAMETER(int32, WeatherResolution)
	SHADER_PARAMETER(FVector4f, WeatherAtlasSizeAndInvSize)
END_SHADER_PARAMETER_STRUCT()

/** Cloud shadow cascades (Phase 4). Must match PA_SHADOW_CASCADES (Shaders/Private/CloudShadowCommon.ush). */
#define PLANET_ATMOSPHERE_SHADOW_CASCADES 3

/**
 * Step 25: two sets of cascades (front = used for lighting, back = rebuilt for a new sun direction / extent level, then
 * crossfaded in) -> 6 cascade slots in the atlas, slot = set x 3 + cascade. Must match PA_SHADOW_SLOTS.
 */
#define PLANET_ATMOSPHERE_SHADOW_SLOTS 6

/** Tiles of cascade texels written by one FAtmosphereCloudShadowGenerateCS dispatch. = PA_SHADOW_MAX_TILES_PER_PASS. */
#define PLANET_ATMOSPHERE_SHADOW_MAX_TILES_PER_PASS 64

/**
 * Cloud shadow cascades of the primary planet of a view (Phase 4 / Step 22, AtmosphereCloudShadows.h). HLSL declarations
 * and helpers: Shaders/Private/CloudShadowCommon.ush. The atlas texture itself is bound separately (SRV in the raymarch,
 * UAV in the generation pass). Every vector is in the PLANET-LOCAL frame of the primary planet, distances in cm.
 * Integers that can exceed int16 are stored in floats (exact below 2^24).
 */
BEGIN_SHADER_PARAMETER_STRUCT(FAtmosphereCloudShadowParameters, )
	// Index of the primary planet in the atmosphere arrays (FAtmosphereInstanceParameters); -1 = no cascades this frame.
	SHADER_PARAMETER(int32, CloudShadowPlanet)
	// Texels per cascade side (multiple of the 32-texel tile); slot s = rows [s * Res, (s + 1) * Res) of the atlas.
	SHADER_PARAMETER(int32, CloudShadowResolution)
	// Step 25: first slot of the front set (lighting) and of the back set; weight of the back set during a crossfade
	// (0 = front only).
	SHADER_PARAMETER(int32, CloudShadowFrontBase)
	SHADER_PARAMETER(int32, CloudShadowBackBase)
	SHADER_PARAMETER(float, CloudShadowBlend)
	// Per slot: xyz = light-plane axis U (unit), w = texel size (cm)
	SHADER_PARAMETER_ARRAY(FVector4f, CloudShadowAxisU, [PLANET_ATMOSPHERE_SHADOW_SLOTS])
	// xyz = light-plane axis V (unit), w = 1 / texel size
	SHADER_PARAMETER_ARRAY(FVector4f, CloudShadowAxisV, [PLANET_ATMOSPHERE_SHADOW_SLOTS])
	// xyz = direction toward the sun this cascade was built for (unit), w = 1 if the cascade is configured
	SHADER_PARAMETER_ARRAY(FVector4f, CloudShadowSun, [PLANET_ATMOSPHERE_SHADOW_SLOTS])
	// xy = global texel index of the window's first texel, zw = sub-camera point in window texels (debug view)
	SHADER_PARAMETER_ARRAY(FVector4f, CloudShadowWindow, [PLANET_ATMOSPHERE_SHADOW_SLOTS])
END_SHADER_PARAMETER_STRUCT()

/** r.PlanetAtmosphere.DebugMode 1 — analytical planet / atmosphere / cloud shells. Shaders/Private/AtmosphereBoundsDebug.usf */
class FAtmosphereBoundsDebugCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FAtmosphereBoundsDebugCS);
	SHADER_USE_PARAMETER_STRUCT(FAtmosphereBoundsDebugCS, FGlobalShader);

	/** Must match [numthreads(8, 8, 1)] in the .usf. */
	static constexpr int32 ThreadGroupSize = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FAtmosphereViewParameters, ViewParams)
		SHADER_PARAMETER_STRUCT_INCLUDE(FAtmosphereInstanceParameters, AtmosphereParams)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
		SHADER_PARAMETER(float, DebugIntensity)
	END_SHADER_PARAMETER_STRUCT()
};

/**
 * r.PlanetAtmosphere.DebugMode 0 / 2 / 3 / 4 / 5 / 6 / 7 — cloud + atmosphere raymarch
 * (final / density / cloud height / ray steps / atmosphere only / transmittance LUT / multiple-scattering LUT).
 * Shaders/Private/CloudRaymarch.usf. Density comes only from CloudDensity.ush (AD-1).
 */
class FAtmosphereCloudRaymarchCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FAtmosphereCloudRaymarchCS);
	SHADER_USE_PARAMETER_STRUCT(FAtmosphereCloudRaymarchCS, FGlobalShader);

	/** Must match [numthreads(8, 8, 1)] in the .usf. */
	static constexpr int32 ThreadGroupSize = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Engine view uniform buffer: View.PreExposure (scene color is pre-exposed), View.StateFrameIndex (jitter).
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FAtmosphereViewParameters, ViewParams)
		SHADER_PARAMETER_STRUCT_INCLUDE(FAtmosphereInstanceParameters, AtmosphereParams)
		// Interleaved rendering (Step 19): one pixel per N x N block, block * N + offset; outputs one texel per block.
		SHADER_PARAMETER(int32, InterleaveFactor)
		SHADER_PARAMETER(int32, InterleaveOffsetX)
		SHADER_PARAMETER(int32, InterleaveOffsetY)
		// Outputs (Step 17): pre-exposed luminance and transmittance, applied to the scene by FAtmosphereCompositeCS.
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutLuminance)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutTransmittance)
		SHADER_PARAMETER(int32, DebugMode)
		SHADER_PARAMETER(int32, bDrawPlanetSurface)
		// Lighting (Step 7). Illuminance in lux; SunDirection points toward the sun (world axes).
		SHADER_PARAMETER(FVector3f, SunDirection)
		SHADER_PARAMETER(int32, bHasSun)
		SHADER_PARAMETER(FVector3f, SunIlluminance)
		SHADER_PARAMETER(int32, LightSteps)
		SHADER_PARAMETER(FVector3f, AmbientIlluminance)
		// Sample distribution (Phase 2 / Step 9). Shaders/Private/RaymarchSchedule.ush.
		SHADER_PARAMETER(int32, StepDistribution)
		SHADER_PARAMETER(float, StepNearDistanceScale)
		SHADER_PARAMETER(float, StepRatioMax)
		SHADER_PARAMETER(int32, JitterMode)
		SHADER_PARAMETER(int32, EmptySpaceSkipSpan)
		SHADER_PARAMETER(float, MinTransmittance)
		// Noise (Phase 2 / Step 10). PlanetAtmosphereNoise.ush. Textures are bound for both sources.
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, BaseNoiseTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, ErosionNoiseTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, NoiseSampler)
		SHADER_PARAMETER(int32, NoiseSource)
		// Density LOD (Phase 2 / Step 12): multiplier of the pixel footprint used for noise octave fading.
		SHADER_PARAMETER(float, NoiseFootprintScale)
		// Light-march LOD by the true pixel footprint (Phase 2 / Step 12, variant 2).
		SHADER_PARAMETER(int32, LightLODEnable)
		SHADER_PARAMETER(float, LightLODFullFootprint)
		SHADER_PARAMETER(float, LightLODMinFootprint)
		SHADER_PARAMETER(int32, LightLODMinSteps)
		// Atmosphere single scattering (Phase 2.5 / Step 13). Since Step 16 the persistent pool of the LUT cache
		// (AtmosphereLutCache.h), always bound; planet i uses row block AtmosphereLutInfo[i].x.
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, TransmittanceLutAtlas)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmittanceLutSampler)
		SHADER_PARAMETER(int32, bAtmosphereEnabled)
		SHADER_PARAMETER(int32, AtmosphereSteps)
		// Multiple scattering (Phase 2.5 / Step 14). Sampled with TransmittanceLutSampler. Pool of the LUT cache; when the
		// MS LUT is not requested (bMultipleScattering = 0) the transmittance pool is bound here instead: never sampled then.
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, MultipleScatteringLutAtlas)
		SHADER_PARAMETER(int32, bMultipleScattering)
		// Clouds through the atmosphere (Phase 2.5 / Step 15): global multiplier of the per-planet CloudSkyAmbientScale.
		SHADER_PARAMETER(float, CloudSkyAmbientScaleMultiplier)
		// Cloud shadow cascades (Phase 4). Without cascades this frame (CloudShadowPlanet = -1) the transmittance LUT pool
		// is bound as the atlas: never sampled then.
		SHADER_PARAMETER_STRUCT_INCLUDE(FAtmosphereCloudShadowParameters, CloudShadowParams)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CloudShadowAtlas)
		// Step 23: lighting of the primary planet's clouds = local march + cascades (fallback: the full light march).
		SHADER_PARAMETER(int32, CloudShadowLighting)
		SHADER_PARAMETER(int32, CloudShadowLocalSteps)
		SHADER_PARAMETER(float, CloudShadowLocalLength)
		// Step 24: cloud shadows on the placeholder surface (cascades where fine enough, else a march through the layer).
		SHADER_PARAMETER(int32, CloudShadowSurface)
		SHADER_PARAMETER(int32, CloudShadowSurfaceSteps)
		SHADER_PARAMETER(int32, CloudShadowSurfaceMaxSteps)
		SHADER_PARAMETER(float, CloudShadowSurfaceMaxStep)
		SHADER_PARAMETER(int32, CloudShadowSurfaceJitter)
		SHADER_PARAMETER(float, CloudShadowSurfaceMaxTexel)
		// Weather (Phase 5): the weather atlas (density, Step 29; debug views 14-17), per planet AtmosphereWeatherInfo.
		SHADER_PARAMETER_STRUCT_INCLUDE(FAtmosphereWeatherAtlasParameters, WeatherParams)
	END_SHADER_PARAMETER_STRUCT()
};

/** Storms per weather snapshot. Must match PA_WEATHER_MAX_STORMS (WeatherModel.ush) and PlanetAtmosphere::Weather::MaxStorms. */
#define PLANET_ATMOSPHERE_WEATHER_MAX_STORMS 64

/**
 * Weather snapshot generation (Phase 5 / Step 27): model C (WeatherModel.ush) at the texel centres of up to 6 cube faces of
 * one planet at one time. Shaders/Private/WeatherGenerate.usf, entry WeatherCS. Dispatch: (Res / 8, Res / 8, faces) groups.
 * Uniforms from PlanetAtmosphere::Weather::FSnapshotInputs (AtmosphereWeatherModel.h), scheduled by AtmosphereWeather.cpp.
 */
class FAtmosphereWeatherGenerateCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FAtmosphereWeatherGenerateCS);
	SHADER_USE_PARAMETER_STRUCT(FAtmosphereWeatherGenerateCS, FGlobalShader);

	/** Must match [numthreads(8, 8, 1)] in the .usf. */
	static constexpr int32 ThreadGroupSize = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutWeatherAtlas)
		SHADER_PARAMETER(int32, WeatherResolution)
		SHADER_PARAMETER(int32, WeatherRowBase)
		// x = cube face written by dispatch slice z
		SHADER_PARAMETER_ARRAY(FVector4f, WeatherFaces, [6])
		// radius km, rotation sign, wind scale, mean humidity
		SHADER_PARAMETER(FVector4f, WeatherPlanet)
		// Hadley edge deg, Ferrel edge deg, thermal equator rad, unused
		SHADER_PARAMETER(FVector4f, WeatherCells)
		// transported noise: anchor ages (h) 0 / 1, crossfade weights 0 / 1
		SHADER_PARAMETER(FVector4f, WeatherNoise)
		// ITCZ clusters: anchor weights 0 / 1, drift angle (rad), unused
		SHADER_PARAMETER(FVector4f, WeatherItcz)
		// easterly wave phases (wavenumber 7, 11), storm-track wave phase, ITCZ meander phase (rad)
		SHADER_PARAMETER(FVector4f, WeatherPhases)
		// uint32 bit patterns of the anchor seeds
		SHADER_PARAMETER(int32, WeatherNoiseSeed0)
		SHADER_PARAMETER(int32, WeatherNoiseSeed1)
		SHADER_PARAMETER(int32, WeatherItczSeed0)
		SHADER_PARAMETER(int32, WeatherItczSeed1)
		// Storms (see WeatherModel.ush)
		SHADER_PARAMETER(int32, NumWeatherStorms)
		SHADER_PARAMETER_ARRAY(FVector4f, WeatherStorm0, [PLANET_ATMOSPHERE_WEATHER_MAX_STORMS])
		SHADER_PARAMETER_ARRAY(FVector4f, WeatherStorm1, [PLANET_ATMOSPHERE_WEATHER_MAX_STORMS])
		SHADER_PARAMETER_ARRAY(FVector4f, WeatherStorm2, [PLANET_ATMOSPHERE_WEATHER_MAX_STORMS])
		SHADER_PARAMETER_ARRAY(FVector4f, WeatherStorm3, [PLANET_ATMOSPHERE_WEATHER_MAX_STORMS])
	END_SHADER_PARAMETER_STRUCT()
};

/**
 * Cloud shadow cascades (Phase 4 / Step 22): writes tiles of 32 x 32 texels of the cascade atlas.
 * Shaders/Private/CloudShadowGenerate.usf, entry GenerateCS. Per texel a march of GenerationSteps samples through the
 * cloud shell along the sun direction -> Beer Shadow Map (front, back, max optical depth, valid). Mode per tile:
 * generate or invalidate (clear). Dispatch: (32 / 8, 32 / 8, NumTiles) groups. Density only from CloudDensity.ush (AD-1).
 */
class FAtmosphereCloudShadowGenerateCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FAtmosphereCloudShadowGenerateCS);
	SHADER_USE_PARAMETER_STRUCT(FAtmosphereCloudShadowGenerateCS, FGlobalShader);

	/** Must match [numthreads(8, 8, 1)] in the .usf. */
	static constexpr int32 ThreadGroupSize = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FAtmosphereInstanceParameters, AtmosphereParams)
		SHADER_PARAMETER_STRUCT_INCLUDE(FAtmosphereCloudShadowParameters, CloudShadowParams)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutShadowAtlas)
		// xy = global tile index (32 x 32 texels), z = slot (set x 3 + cascade), w = 1 generate / 0 invalidate
		SHADER_PARAMETER_ARRAY(FVector4f, ShadowTiles, [PLANET_ATMOSPHERE_SHADOW_MAX_TILES_PER_PASS])
		SHADER_PARAMETER(int32, NumShadowTiles)
		SHADER_PARAMETER(int32, ShadowGenerationSteps)
		// Same noise inputs as the raymarch (PlanetAtmosphereNoise.ush).
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, BaseNoiseTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, ErosionNoiseTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, NoiseSampler)
		SHADER_PARAMETER(int32, NoiseSource)
		// r.PlanetAtmosphere.NoiseFootprintScale: noise octaves fade at this fraction of the texel size (prototype: 1/4).
		SHADER_PARAMETER(float, NoiseFootprintScale)
		// Step 29: the weather drives the coverage in the density (same atlas as the raymarch).
		SHADER_PARAMETER_STRUCT_INCLUDE(FAtmosphereWeatherAtlasParameters, WeatherParams)
	END_SHADER_PARAMETER_STRUCT()
};

/**
 * Invalidates whole cascade slots of the atlas (alpha 0): new atlas, or a set whose configuration changed.
 * Shaders/Private/CloudShadowGenerate.usf, entry ClearCS. Dispatch: Res x (6 Res) threads.
 */
class FAtmosphereCloudShadowClearCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FAtmosphereCloudShadowClearCS);
	SHADER_USE_PARAMETER_STRUCT(FAtmosphereCloudShadowClearCS, FGlobalShader);

	/** Must match [numthreads(8, 8, 1)] in the .usf. */
	static constexpr int32 ThreadGroupSize = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutShadowAtlas)
		SHADER_PARAMETER(int32, ShadowAtlasResolution)
		// bit s = clear slot s
		SHADER_PARAMETER(int32, ClearCascadeMask)
	END_SHADER_PARAMETER_STRUCT()
};

/**
 * Composite of the raymarch result over the scene (Phase 3 / Step 17): final = SceneColor x Transmittance + Luminance.
 * Shaders/Private/AtmosphereComposite.usf. The only pass that reads the scene color (besides the bounds debug view).
 */
class FAtmosphereCompositeCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FAtmosphereCompositeCS);
	SHADER_USE_PARAMETER_STRUCT(FAtmosphereCompositeCS, FGlobalShader);

	/** Must match [numthreads(8, 8, 1)] in the .usf. */
	static constexpr int32 ThreadGroupSize = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, LuminanceTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, TransmittanceTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
		SHADER_PARAMETER(FVector4f, ViewRectMinAndSize)
		// r.PlanetAtmosphere.DebugMode 8 (Step 18): overlay of the current-frame weight decoded from transmittance.a.
		SHADER_PARAMETER(int32, bShowTemporalWeight)
		SHADER_PARAMETER(float, CurrentFrameWeight)
	END_SHADER_PARAMETER_STRUCT()
};

/** Reprojection slots of the temporal pass: one per visible atmosphere + camera-only. = PA_REPROJ_SLOTS (PlanetAtmosphereShaderData.ush). */
#define PLANET_ATMOSPHERE_REPROJ_SLOTS (PLANET_ATMOSPHERE_MAX_VISIBLE + 1)

/**
 * Temporal accumulation of the raymarch result (Phase 3 / Step 18). Shaders/Private/AtmosphereTemporal.usf.
 * Reads the current luminance / transmittance (+ reprojection depth / planet in alpha) and the previous history,
 * writes the new history (also the input of the composite). Set up by AtmosphereTemporal.cpp.
 */
class FAtmosphereTemporalCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FAtmosphereTemporalCS);
	SHADER_USE_PARAMETER_STRUCT(FAtmosphereTemporalCS, FGlobalShader);

	/** Must match [numthreads(8, 8, 1)] in the .usf. */
	static constexpr int32 ThreadGroupSize = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Engine view uniform buffer: View.PreExposure (history exposure correction).
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FAtmosphereViewParameters, ViewParams)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CurrentLuminance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CurrentTransmittance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, HistoryLuminance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, HistoryTransmittance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, HistoryExposure)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, HistorySlot)
		SHADER_PARAMETER_SAMPLER(SamplerState, HistorySampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutLuminance)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutTransmittance)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutExposure)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutSlot)
		// Row-vector reprojection matrices (current camera-relative point -> previous clip), one per slot.
		SHADER_PARAMETER_ARRAY(FVector4f, ReprojRow0, [PLANET_ATMOSPHERE_REPROJ_SLOTS])
		SHADER_PARAMETER_ARRAY(FVector4f, ReprojRow1, [PLANET_ATMOSPHERE_REPROJ_SLOTS])
		SHADER_PARAMETER_ARRAY(FVector4f, ReprojRow2, [PLANET_ATMOSPHERE_REPROJ_SLOTS])
		SHADER_PARAMETER_ARRAY(FVector4f, ReprojRow3, [PLANET_ATMOSPHERE_REPROJ_SLOTS])
		// x = slot of the same planet in the previous frame, -1 = new planet.
		SHADER_PARAMETER_ARRAY(FVector4f, PlanetPrevSlot, [PLANET_ATMOSPHERE_REPROJ_SLOTS])
		SHADER_PARAMETER(FVector4f, CurrentJitterNDC)
		SHADER_PARAMETER(FVector4f, CurrentViewForward)
		SHADER_PARAMETER(FVector4f, HistoryTextureSizeAndInvSize)
		SHADER_PARAMETER(FVector4f, PrevViewRectMinAndSize)
		SHADER_PARAMETER(int32, bHistoryValid)
		SHADER_PARAMETER(float, CurrentFrameWeight)
		SHADER_PARAMETER(float, ClampGamma)
		SHADER_PARAMETER(float, DepthRejectRatio)
		// Interleaved reconstruction (Step 19).
		SHADER_PARAMETER(int32, InterleaveFactor)
		SHADER_PARAMETER(int32, InterleaveOffsetX)
		SHADER_PARAMETER(int32, InterleaveOffsetY)
		SHADER_PARAMETER(float, StaticClampGamma)
		SHADER_PARAMETER(float, ClampMotionPixels)
	END_SHADER_PARAMETER_STRUCT()
};

/**
 * Transmittance LUT (Phase 2.5 / Step 13). Shaders/Private/TransmittanceLut.usf. Since Step 16 dispatched only for the
 * atmospheres whose LUT must be (re)built (compacted FAtmosphereInstanceParameters, AtmosphereLutCache.cpp):
 * Width x (Height x NumAtmospheres) threads, entry i written to row block AtmosphereLutInfo[i].x of the pool.
 */
class FAtmosphereTransmittanceLutCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FAtmosphereTransmittanceLutCS);
	SHADER_USE_PARAMETER_STRUCT(FAtmosphereTransmittanceLutCS, FGlobalShader);

	/** Must match [numthreads(8, 8, 1)] in the .usf. */
	static constexpr int32 ThreadGroupSize = 8;

	/** Must match PA_TRANSMITTANCE_LUT_WIDTH / HEIGHT in Shaders/Private/AtmosphereScattering.ush. */
	static constexpr int32 LutWidth = 256;
	static constexpr int32 LutHeight = 64;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FAtmosphereInstanceParameters, AtmosphereParams)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutTransmittanceLut)
	END_SHADER_PARAMETER_STRUCT()
};

/**
 * Multiple-scattering LUT (Phase 2.5 / Step 14, Hillaire 2020). Shaders/Private/MultipleScatteringLut.usf.
 * Like the transmittance LUT, dispatched only for the atmospheres to (re)build (Step 16): LutWidth x (LutHeight x
 * NumAtmospheres) thread groups, one per texel, one thread per direction, groupshared sum; entry i written to row block
 * AtmosphereLutInfo[i].x of the pool. Reads the transmittance pool (already rebuilt earlier in the same graph).
 */
class FAtmosphereMultipleScatteringLutCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FAtmosphereMultipleScatteringLutCS);
	SHADER_USE_PARAMETER_STRUCT(FAtmosphereMultipleScatteringLutCS, FGlobalShader);

	/** Must match PA_MS_LUT_WIDTH / HEIGHT in Shaders/Private/AtmosphereScattering.ush. */
	static constexpr int32 LutWidth = 64;    // sun cos zenith
	static constexpr int32 LutHeight = 32;   // altitude

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FAtmosphereInstanceParameters, AtmosphereParams)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, TransmittanceLutAtlas)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmittanceLutSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutMultipleScatteringLut)
	END_SHADER_PARAMETER_STRUCT()
};

/**
 * Bakes one mip of a tileable 3D noise texture (Phase 2 / Step 10). Shaders/Private/NoiseBake.usf.
 * Dispatched only when the shared noise textures are created (AtmosphereNoiseTextures.cpp).
 */
class FAtmosphereNoiseBakeCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FAtmosphereNoiseBakeCS);
	SHADER_USE_PARAMETER_STRUCT(FAtmosphereNoiseBakeCS, FGlobalShader);

	/** Must match [numthreads(4, 4, 4)] in the .usf. */
	static constexpr int32 ThreadGroupSize = 4;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, OutNoise)
		SHADER_PARAMETER(int32, MipSize)
		SHADER_PARAMETER(int32, Octaves)
		SHADER_PARAMETER(float, TexelSize)
	END_SHADER_PARAMETER_STRUCT()
};
