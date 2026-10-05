// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

class FAtmosphereProxyRegistry;

class FPlanetAtmosphereViewExtension : public FSceneViewExtensionBase
{
public:
	FPlanetAtmosphereViewExtension(const FAutoRegister& AutoRegister, TSharedPtr<FAtmosphereProxyRegistry, ESPMode::ThreadSafe> InRegistry);
	virtual ~FPlanetAtmosphereViewExtension();

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override;

private:
	TSharedPtr<FAtmosphereProxyRegistry, ESPMode::ThreadSafe> Registry;
};
