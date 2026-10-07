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
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
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
	// x = erosion, y = raymarch steps, z = light steps (both after screen-space LOD, Step 11), w = reserved
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
		// Atmosphere single scattering (Phase 2.5 / Step 13). The atlas is always bound (built every frame, see renderer).
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, TransmittanceLutAtlas)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmittanceLutSampler)
		SHADER_PARAMETER(int32, bAtmosphereEnabled)
		SHADER_PARAMETER(int32, AtmosphereSteps)
		// Multiple scattering (Phase 2.5 / Step 14). Sampled with TransmittanceLutSampler. When the MS LUT is not built
		// (bMultipleScattering = 0) the transmittance atlas is bound here instead: the shader never samples it then.
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, MultipleScatteringLutAtlas)
		SHADER_PARAMETER(int32, bMultipleScattering)
	END_SHADER_PARAMETER_STRUCT()
};

/**
 * Transmittance LUT of every visible atmosphere (Phase 2.5 / Step 13). Shaders/Private/TransmittanceLut.usf.
 * Writes a per-frame transient atlas: Width x (Height x NumAtmospheres), one row block per planet in the order of
 * FAtmosphereInstanceParameters (the same filled struct is used by the raymarch pass).
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
 * Multiple-scattering LUT of every visible atmosphere (Phase 2.5 / Step 14, Hillaire 2020).
 * Shaders/Private/MultipleScatteringLut.usf. Per-frame transient atlas LutWidth x (LutHeight x NumAtmospheres), same
 * planet order as FAtmosphereInstanceParameters. One thread group per texel (dispatch = atlas size), one thread per
 * direction, groupshared sum. Reads the transmittance LUT atlas of the same frame.
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
