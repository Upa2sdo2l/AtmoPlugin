// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

class FAtmosphereProxyRegistry;
class UWorld;

/**
 * One instance per UWorld, owned by UAtmosphereWorldSubsystem (the engine keeps only a weak ref).
 *
 * Derives from FWorldSceneViewExtension (UE 5.6: Runtime/Engine/Public/SceneViewExtension.h,
 * ctor FWorldSceneViewExtension(const FAutoRegister&, UWorld*)), whose IsActiveThisFrame_Internal
 * restricts it to view families rendering this extension's world.
 *
 * NOTE: PreRenderView_RenderThread runs before the renderer's InitViews (no scene depth yet).
 * It is kept only for culling diagnostics; the render hook moves in Step 5.
 */
class FPlanetAtmosphereViewExtension : public FWorldSceneViewExtension
{
public:
	FPlanetAtmosphereViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld, TSharedPtr<FAtmosphereProxyRegistry, ESPMode::ThreadSafe> InRegistry);
	virtual ~FPlanetAtmosphereViewExtension();

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override;

private:
	TSharedPtr<FAtmosphereProxyRegistry, ESPMode::ThreadSafe> Registry;
};
