// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphResources.h"
#include "PlanetAtmosphereTypes.h"

/**
 * Step 5: analytical "Atmosphere Bounds" debug visualization.
 * Reads scene color + scene depth, writes a new scene color with planet / atmosphere / cloud shells overlaid.
 * Shader: Shaders/Private/AtmosphereBoundsDebug.usf (MainCS).
 *
 * Per-atmosphere data is passed as small constant arrays (<= PLANET_ATMOSPHERE_MAX_VISIBLE entries),
 * with the camera position RELATIVE to each planet computed on the CPU in double precision.
 * Sphere radii are passed as altitudes above the planet radius, together with the camera altitude
 * (also computed in double): see the precision note in PlanetAtmosphereCommon.ush.
 */
class FAtmosphereBoundsDebugCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FAtmosphereBoundsDebugCS);
	SHADER_USE_PARAMETER_STRUCT(FAtmosphereBoundsDebugCS, FGlobalShader);

	/** Must match [numthreads(8, 8, 1)] in the .usf. */
	static constexpr int32 ThreadGroupSize = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
		SHADER_PARAMETER(FMatrix44f, ClipToTranslatedWorld)
		SHADER_PARAMETER(FVector3f, CameraTranslatedWorld)
		SHADER_PARAMETER(FVector4f, ViewRectMinAndSize)
		SHADER_PARAMETER(int32, NumAtmospheres)
		SHADER_PARAMETER(float, DebugIntensity)
		// xyz = camera - planet center (cm), w = planet radius (cm)
		SHADER_PARAMETER_ARRAY(FVector4f, AtmosphereCameraRelAndPlanetRadius, [PLANET_ATMOSPHERE_MAX_VISIBLE])
		// x = camera altitude above planet radius, y = atmosphere bottom, z = atmosphere top, w = cloud bottom (altitudes, cm)
		SHADER_PARAMETER_ARRAY(FVector4f, AtmosphereAltitudes0, [PLANET_ATMOSPHERE_MAX_VISIBLE])
		// x = cloud top altitude (cm), yzw = reserved
		SHADER_PARAMETER_ARRAY(FVector4f, AtmosphereAltitudes1, [PLANET_ATMOSPHERE_MAX_VISIBLE])
	END_SHADER_PARAMETER_STRUCT()
};
