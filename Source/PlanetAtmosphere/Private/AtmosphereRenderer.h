// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PlanetAtmosphereTypes.h"

class FRDGBuilder;
class FSceneView;
struct FPostProcessMaterialInputs;
struct FScreenPassTexture;

namespace PlanetAtmosphere
{
	/**
	 * Render thread. Adds the PlanetAtmosphere pass for one view according to r.PlanetAtmosphere.DebugMode:
	 *   0 = Final Clouds, 2 = Density  -> FAtmosphereCloudRaymarchCS
	 *   1 = Atmosphere Bounds          -> FAtmosphereBoundsDebugCS
	 *
	 * Called from the EPostProcessingPass::BeforeDOF callback (HDR scene color, before DOF/TSR/tonemapping).
	 * Returns the new scene color. If there is nothing to draw (no instances, missing inputs,
	 * an override output is requested), returns Inputs.ReturnUntouchedSceneColorForPostProcessing().
	 *
	 * Instances are sorted near -> far in place and truncated to r.PlanetAtmosphere.MaxVisible.
	 * Only POD copies are used, nothing references scene proxies.
	 */
	FScreenPassTexture AddAtmospherePasses(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs,
		TArray<FAtmosphereVisibleInstance>& Instances);
}
