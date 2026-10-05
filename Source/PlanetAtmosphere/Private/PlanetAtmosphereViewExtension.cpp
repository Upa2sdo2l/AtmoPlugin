// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlanetAtmosphereViewExtension.h"
#include "AtmosphereProxyRegistry.h"
#include "PlanetAtmosphereSceneProxy.h"
#include "SceneView.h"
#include "RenderGraphBuilder.h"

FPlanetAtmosphereViewExtension::FPlanetAtmosphereViewExtension(
	const FAutoRegister& AutoRegister,
	TSharedPtr<FAtmosphereProxyRegistry, ESPMode::ThreadSafe> InRegistry)
	: FSceneViewExtensionBase(AutoRegister)
	, Registry(InRegistry)
{
	UE_LOG(LogTemp, Log, TEXT("PlanetAtmosphereViewExtension: Created"));
}

FPlanetAtmosphereViewExtension::~FPlanetAtmosphereViewExtension()
{
	UE_LOG(LogTemp, Log, TEXT("PlanetAtmosphereViewExtension: Destroyed"));
}

void FPlanetAtmosphereViewExtension::PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView)
{
	check(IsInRenderingThread());

	if (!Registry.IsValid())
	{
		return;
	}

	TArray<FPlanetAtmosphereSceneProxy*> VisibleProxies = Registry->GetVisibleProxies(InView);

	if (VisibleProxies.Num() > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("ViewExtension: %d visible atmosphere(s)"), VisibleProxies.Num());
	}
}
