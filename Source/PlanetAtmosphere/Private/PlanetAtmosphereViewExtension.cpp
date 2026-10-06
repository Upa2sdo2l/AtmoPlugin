// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlanetAtmosphereViewExtension.h"
#include "AtmosphereProxyRegistry.h"
#include "AtmosphereRenderer.h"
#include "AtmosphereCVars.h"
#include "PlanetAtmosphereTypes.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "ScreenPass.h"
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

void FPlanetAtmosphereViewExtension::SubscribeToPostProcessingPass(
	EPostProcessingPass Pass,
	const FSceneView& InView,
	FPostProcessingPassDelegateArray& InOutPassCallbacks,
	bool bIsPassEnabled)
{
	if (Pass != EPostProcessingPass::BeforeDOF)
	{
		return;
	}

	// r.PlanetAtmosphere.Enable 0 -> nothing is subscribed, so no callback and no GPU work at all.
	if (!Registry.IsValid() || !PlanetAtmosphere::CVars::IsEnabled())
	{
		return;
	}

	// Scene captures / reflection captures / planar reflections are not handled yet.
	if (InView.bIsSceneCapture || InView.bIsReflectionCapture || InView.bIsPlanarReflection)
	{
		return;
	}

	if (!bIsPassEnabled)
	{
		UE_LOG(LogPlanetAtmosphere, Verbose, TEXT("ViewExtension: BeforeDOF pass reported as disabled for this view, not subscribing"));
		return;
	}

	// CreateRaw is safe: the view family holds a strong reference to this extension while the frame renders.
	InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FPlanetAtmosphereViewExtension::PostProcessBeforeDOF_RenderThread));
}

FScreenPassTexture FPlanetAtmosphereViewExtension::PostProcessBeforeDOF_RenderThread(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs)
{
	TArray<FAtmosphereVisibleInstance> VisibleInstances;
	const int32 RegisteredCount = Registry->GatherVisibleInstances(View, VisibleInstances);

	// Per-frame diagnostics: enable with console command `log LogPlanetAtmosphere Verbose`.
	UE_LOG(LogPlanetAtmosphere, Verbose, TEXT("ViewExtension: %d of %d atmosphere(s) pass plugin frustum culling"),
		VisibleInstances.Num(), RegisteredCount);

	return PlanetAtmosphere::AddAtmospherePasses(GraphBuilder, View, Inputs, VisibleInstances);
}
