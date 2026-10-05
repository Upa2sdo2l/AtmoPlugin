// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlanetAtmosphereSceneProxy.h"
#include "PlanetAtmosphereComponent.h"
#include "AtmosphereWorldSubsystem.h"
#include "AtmosphereProxyRegistry.h"
#include "Engine/World.h"

FPlanetAtmosphereSceneProxy::FPlanetAtmosphereSceneProxy(const UPlanetAtmosphereComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
	, RadiiUU(InComponent->GetValidatedRadiiUU())
	, CloudCoverage(InComponent->CloudCoverage)
	, CloudDensity(InComponent->CloudDensity)
	, RaymarchSteps(InComponent->RaymarchSteps)
{
	// Grab a shared reference to the plain-C++ registry.
	// After this point the proxy never touches any UObject.
	if (const UWorld* World = InComponent->GetWorld())
	{
		if (const UAtmosphereWorldSubsystem* Subsystem = World->GetSubsystem<UAtmosphereWorldSubsystem>())
		{
			Registry = Subsystem->GetProxyRegistry();
		}
	}
}

FPlanetAtmosphereSceneProxy::~FPlanetAtmosphereSceneProxy()
{
	// Safety net: should already be unregistered in DestroyRenderThreadResources().
	if (bRegistered && Registry.IsValid())
	{
		Registry->Remove(this);
	}
}

void FPlanetAtmosphereSceneProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
{
	FPrimitiveSceneProxy::CreateRenderThreadResources(RHICmdList);

	if (Registry.IsValid())
	{
		Registry->Add(this);
		bRegistered = true;
	}
}

void FPlanetAtmosphereSceneProxy::DestroyRenderThreadResources()
{
	if (bRegistered && Registry.IsValid())
	{
		Registry->Remove(this);
		bRegistered = false;
	}

	FPrimitiveSceneProxy::DestroyRenderThreadResources();
}

FAtmosphereVisibleInstance FPlanetAtmosphereSceneProxy::MakeVisibleInstance() const
{
	FAtmosphereVisibleInstance Instance;
	Instance.PlanetCenterWorld = GetPlanetCenterWorld();
	Instance.RadiiUU = RadiiUU;
	Instance.CloudCoverage = CloudCoverage;
	Instance.CloudDensity = CloudDensity;
	Instance.RaymarchSteps = RaymarchSteps;
	return Instance;
}

void FPlanetAtmosphereSceneProxy::GetDynamicMeshElements(
	const TArray<const FSceneView*>& Views,
	const FSceneViewFamily& ViewFamily,
	uint32 VisibilityMap,
	FMeshElementCollector& Collector) const
{
	// No mesh rendering — volumetric atmosphere is rendered by compute passes (Step 5+).
}

FPrimitiveViewRelevance FPlanetAtmosphereSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View);
	Result.bDynamicRelevance = true;
	Result.bRenderCustomDepth = false;
	Result.bRenderInMainPass = false;
	Result.bUsesLightingChannels = false;
	Result.bTranslucentSelfShadow = false;
	Result.bShadowRelevance = false;
	return Result;
}

SIZE_T FPlanetAtmosphereSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}
