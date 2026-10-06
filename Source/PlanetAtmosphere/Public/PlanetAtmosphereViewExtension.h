// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

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
 * Render hook (Step 5): SubscribeToPostProcessingPass(EPostProcessingPass::BeforeDOF) — public API,
 * HDR scene color + scene depth available, before DOF / TSR / tonemapping. The callback gathers
 * visible atmospheres (plugin frustum culling) and returns the new scene color.
 */
class FPlanetAtmosphereViewExtension : public FWorldSceneViewExtension
{
public:
	FPlanetAtmosphereViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld, TSharedPtr<FAtmosphereProxyRegistry, ESPMode::ThreadSafe> InRegistry);
	virtual ~FPlanetAtmosphereViewExtension();

	//~ Begin ISceneViewExtension Interface
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView, FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;
	//~ End ISceneViewExtension Interface

private:
	/** Render thread: EPostProcessingPass::BeforeDOF callback. */
	FScreenPassTexture PostProcessBeforeDOF_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs);

	TSharedPtr<FAtmosphereProxyRegistry, ESPMode::ThreadSafe> Registry;
};
