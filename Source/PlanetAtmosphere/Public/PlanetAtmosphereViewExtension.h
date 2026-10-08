// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "PlanetAtmosphereTypes.h"

class FAtmosphereProxyRegistry;
class UWorld;
struct FPostProcessMaterialInputs;
struct FScreenPassTexture;

/**
 * One instance per UWorld, owned by UAtmosphereWorldSubsystem (the engine keeps only a weak ref,
 * and holds a strong ref for the duration of each rendered frame).
 *
 * Derives from FWorldSceneViewExtension (UE 5.6: Runtime/Engine/Public/SceneViewExtension.h,
 * ctor FWorldSceneViewExtension(const FAutoRegister&, UWorld*)), whose IsActiveThisFrame_Internal
 * restricts it to view families rendering this extension's world.
 *
 * Render hook: SubscribeToPostProcessingPass(EPostProcessingPass::BeforeDOF) — public API,
 * HDR scene color + scene depth available, before DOF / TSR / tonemapping. The callback gathers
 * visible atmospheres (plugin frustum culling) and returns the new scene color
 * (clouds, or a debug view selected by r.PlanetAtmosphere.DebugMode).
 */
class FPlanetAtmosphereViewExtension : public FWorldSceneViewExtension
{
public:
	FPlanetAtmosphereViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld, TSharedPtr<FAtmosphereProxyRegistry, ESPMode::ThreadSafe> InRegistry);
	virtual ~FPlanetAtmosphereViewExtension();

	//~ Begin ISceneViewExtension Interface
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	/** Game thread: picks the sun (Directional Light), reads the weather clock and sends copies to the render thread. */
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView, FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;
	//~ End ISceneViewExtension Interface

private:
	/** Render thread: EPostProcessingPass::BeforeDOF callback. */
	FScreenPassTexture PostProcessBeforeDOF_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs);

	TSharedPtr<FAtmosphereProxyRegistry, ESPMode::ThreadSafe> Registry;

	/** Render thread only: written by the command enqueued in BeginRenderViewFamily, read by the pass. */
	FAtmosphereSunLight SunLight_RenderThread;
	FAtmosphereWeatherTime WeatherTime_RenderThread;
};
