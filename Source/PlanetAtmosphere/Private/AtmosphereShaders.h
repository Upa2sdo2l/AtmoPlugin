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
	// x = erosion, y = raymarch steps, zw = reserved
	SHADER_PARAMETER_ARRAY(FVector4f, AtmosphereData3, [PLANET_ATMOSPHERE_MAX_VISIBLE])
	// Planet local axes in world space (unit); clouds are evaluated in this frame.
	SHADER_PARAMETER_ARRAY(FVector4f, AtmosphereAxisX, [PLANET_ATMOSPHERE_MAX_VISIBLE])
	SHADER_PARAMETER_ARRAY(FVector4f, AtmosphereAxisY, [PLANET_ATMOSPHERE_MAX_VISIBLE])
	SHADER_PARAMETER_ARRAY(FVector4f, AtmosphereAxisZ, [PLANET_ATMOSPHERE_MAX_VISIBLE])
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
 * r.PlanetAtmosphere.DebugMode 0 / 2 — cloud raymarch (final clouds / density view).
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
		// Engine view uniform buffer: only View.PreExposure is used (scene color is pre-exposed).
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
	END_SHADER_PARAMETER_STRUCT()
};
