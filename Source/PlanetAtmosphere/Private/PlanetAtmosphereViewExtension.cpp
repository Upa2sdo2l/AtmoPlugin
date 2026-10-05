// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlanetAtmosphereViewExtension.h"
#include "AtmosphereProxyRegistry.h"
#include "PlanetAtmosphereTypes.h"
#include "SceneView.h"
#include "Engine/World.h"
#include <atomic>

namespace
{
	/** Diagnostics: number of live extensions (expected: one per Editor/Game/PIE world). */
	std::atomic<int32> GLiveViewExtensionCount{0};
}

FPlanetAtmosphereViewExtension::FPlanetAtmosphereViewExtension(
	const FAutoRegister& AutoRegister,
	UWorld* InWorld,
	TSharedPtr<FAtmosphereProxyRegistry, ESPMode::ThreadSafe> InRegistry)
	: FWorldSceneViewExtension(AutoRegister, InWorld)
	, Registry(MoveTemp(InRegistry))
{
	const int32 Live = ++GLiveViewExtensionCount;
	UE_LOG(LogPlanetAtmosphere, Log, TEXT("PlanetAtmosphereViewExtension: Created for world '%s' (live: %d)"),
		*GetNameSafe(InWorld), Live);
}

FPlanetAtmosphereViewExtension::~FPlanetAtmosphereViewExtension()
{
	// May run on the render thread (if it held the last ref) — do not touch the UWorld here.
	const int32 Live = --GLiveViewExtensionCount;
	UE_LOG(LogPlanetAtmosphere, Log, TEXT("PlanetAtmosphereViewExtension: Destroyed (live: %d)"), Live);
}

void FPlanetAtmosphereViewExtension::PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView)
{
	check(IsInRenderingThread());

	if (!Registry.IsValid())
	{
		return;
	}

	// Scene captures / reflection captures / planar reflections are not handled yet.
	if (InView.bIsSceneCapture || InView.bIsReflectionCapture || InView.bIsPlanarReflection)
	{
		return;
	}

	TArray<FAtmosphereVisibleInstance> VisibleInstances;
	const int32 RegisteredCount = Registry->GatherVisibleInstances(InView, VisibleInstances);

	// Per-frame diagnostics: enable with console command `log LogPlanetAtmosphere Verbose`.
	UE_LOG(LogPlanetAtmosphere, Verbose, TEXT("ViewExtension: %d of %d atmosphere(s) pass plugin frustum culling"),
		VisibleInstances.Num(), RegisteredCount);
}
