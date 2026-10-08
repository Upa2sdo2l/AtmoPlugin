// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlanetAtmosphereViewExtension.h"
#include "AtmosphereProxyRegistry.h"
#include "AtmosphereWorldSubsystem.h"
#include "AtmosphereRenderer.h"
#include "AtmosphereCVars.h"
#include "PlanetAtmosphereTypes.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "ScreenPass.h"
#include "SceneView.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "EngineUtils.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "AtmosphereStats.h"
#include <atomic>

DECLARE_CYCLE_STAT(TEXT("Find Sun Light (GT)"), STAT_PlanetAtmosphere_FindSun, STATGROUP_PlanetAtmosphere);
DECLARE_CYCLE_STAT(TEXT("Gather Visible Atmospheres (RT)"), STAT_PlanetAtmosphere_Gather, STATGROUP_PlanetAtmosphere);

namespace
{
	/** Diagnostics: number of live extensions (expected: one per Editor/Game/PIE world). */
	std::atomic<int32> GLiveViewExtensionCount{0};

	/**
	 * Game thread. Picks the sun: the first visible Directional Light marked "Atmosphere Sun Light"
	 * with index 0 (the flag Unreal uses for its own sky); otherwise the first visible Directional Light.
	 */
	FAtmosphereSunLight FindSunLight_GameThread(UWorld* InWorld)
	{
		SCOPE_CYCLE_COUNTER(STAT_PlanetAtmosphere_FindSun);

		FAtmosphereSunLight Sun;
		if (!InWorld)
		{
			return Sun;
		}

		const UDirectionalLightComponent* Chosen = nullptr;
		for (TActorIterator<ADirectionalLight> It(InWorld); It; ++It)
		{
			const UDirectionalLightComponent* LightComponent = Cast<UDirectionalLightComponent>(It->GetLightComponent());
			if (!LightComponent || !LightComponent->IsVisible())
			{
				continue;
			}
			if (LightComponent->bAtmosphereSunLight && LightComponent->AtmosphereSunLightIndex == 0)
			{
				Chosen = LightComponent;
				break;
			}
			if (!Chosen)
			{
				Chosen = LightComponent;
			}
		}

		if (Chosen)
		{
			// GetDirection() is the direction the light travels; the sun is the opposite way.
			Sun.DirectionToSun = (-Chosen->GetDirection()).GetSafeNormal();
			Sun.Illuminance = Chosen->GetColoredLightBrightness();
			Sun.bValid = !Sun.DirectionToSun.IsNearlyZero();
		}
		return Sun;
	}
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

void FPlanetAtmosphereViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	// Game thread: read the sun from UObjects here, hand a plain copy to the render thread.
	// The command captures a strong reference, so the extension outlives the command even if the
	// world subsystem releases it in the meantime. Commands execute in order, before this family renders.
	UWorld* ViewWorld = GetWorld();
	const FAtmosphereSunLight Sun = FindSunLight_GameThread(ViewWorld);

	// Phase 5 / Step 27: the world's weather clock (one value for every view of this world in this frame).
	FAtmosphereWeatherTime WeatherTime;
	if (ViewWorld)
	{
		if (const UAtmosphereWorldSubsystem* Subsystem = ViewWorld->GetSubsystem<UAtmosphereWorldSubsystem>())
		{
			WeatherTime.Seconds = Subsystem->GetWeatherTime();
			WeatherTime.bValid = true;
		}
	}

	TSharedRef<FPlanetAtmosphereViewExtension, ESPMode::ThreadSafe> Self =
		StaticCastSharedRef<FPlanetAtmosphereViewExtension>(AsShared());

	ENQUEUE_RENDER_COMMAND(PlanetAtmosphereUpdateSun)([Self, Sun, WeatherTime](FRHICommandListImmediate&)
	{
		Self->SunLight_RenderThread = Sun;
		Self->WeatherTime_RenderThread = WeatherTime;
	});
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
	int32 RegisteredCount = 0;
	{
		SCOPE_CYCLE_COUNTER(STAT_PlanetAtmosphere_Gather);
		RegisteredCount = Registry->GatherVisibleInstances(View, VisibleInstances);
	}

	// Per-frame diagnostics: enable with console command `log LogPlanetAtmosphere Verbose`.
	UE_LOG(LogPlanetAtmosphere, Verbose, TEXT("ViewExtension: %d of %d atmosphere(s) pass plugin frustum culling"),
		VisibleInstances.Num(), RegisteredCount);

	return PlanetAtmosphere::AddAtmospherePasses(GraphBuilder, View, Inputs, VisibleInstances, SunLight_RenderThread, WeatherTime_RenderThread);
}
