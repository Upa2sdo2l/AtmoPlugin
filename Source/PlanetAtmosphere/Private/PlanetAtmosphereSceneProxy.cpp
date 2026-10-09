// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlanetAtmosphereSceneProxy.h"
#include "PlanetAtmosphereComponent.h"
#include "AtmosphereWorldSubsystem.h"
#include "AtmosphereProxyRegistry.h"
#include "Engine/World.h"

FPlanetAtmosphereSceneProxy::FPlanetAtmosphereSceneProxy(const UPlanetAtmosphereComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
	, PlanetId(InComponent->GetUniqueID())
	, RadiiUU(InComponent->GetValidatedRadiiUU())
	// Runtime-safe clamps (UPROPERTY ClampMin/Max only applies to Details-panel edits).
	, CloudCoverage(FMath::Clamp(InComponent->CloudCoverage, 0.0f, 1.0f))
	, CloudDensity(FMath::Clamp(InComponent->CloudDensity, 0.0f, 10.0f))
	, CloudShapeScaleUU(FMath::Clamp(InComponent->CloudShapeScale, 100.0, 1000000.0) * PlanetAtmosphere::MetersToUnrealUnits)
	, CloudErosion(FMath::Clamp(InComponent->CloudErosion, 0.0f, 1.0f))
	, CloudClusterScaleUU(FMath::Clamp(InComponent->CloudClusterScale, 1000.0, 10000000.0) * PlanetAtmosphere::MetersToUnrealUnits)
	, CloudMesoScaleUU(FMath::Clamp(InComponent->CloudMesoScale, 100.0, 1000000.0) * PlanetAtmosphere::MetersToUnrealUnits)
	, CloudFairWeatherDepthUU(FMath::Clamp(InComponent->CloudFairWeatherDepth, 50.0, 100000.0) * PlanetAtmosphere::MetersToUnrealUnits)
	, RaymarchSteps(FMath::Clamp(InComponent->RaymarchSteps, 16, 256))
	, CloudSkyAmbientScale(FMath::Clamp(InComponent->CloudSkyAmbientScale, 0.0f, 100.0f))
	, ScatteringUU(InComponent->GetValidatedScatteringUU())
	, Weather(InComponent->GetValidatedWeatherParameters())
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
	const FMatrix& PrimitiveLocalToWorld = GetLocalToWorld();

	FAtmosphereVisibleInstance Instance;
	Instance.PlanetId = PlanetId;
	Instance.PlanetCenterWorld = PrimitiveLocalToWorld.GetOrigin();

	// Rows 0..2 of UE's (row-vector) matrix are the local X/Y/Z axes in world space, scaled.
	// Normalize to remove the actor scale (radii are absolute, see UPlanetAtmosphereComponent).
	Instance.PlanetAxisX = FVector3d(PrimitiveLocalToWorld.M[0][0], PrimitiveLocalToWorld.M[0][1], PrimitiveLocalToWorld.M[0][2]).GetSafeNormal();
	Instance.PlanetAxisY = FVector3d(PrimitiveLocalToWorld.M[1][0], PrimitiveLocalToWorld.M[1][1], PrimitiveLocalToWorld.M[1][2]).GetSafeNormal();
	Instance.PlanetAxisZ = FVector3d(PrimitiveLocalToWorld.M[2][0], PrimitiveLocalToWorld.M[2][1], PrimitiveLocalToWorld.M[2][2]).GetSafeNormal();

	Instance.RadiiUU = RadiiUU;
	Instance.CloudCoverage = CloudCoverage;
	Instance.CloudDensity = CloudDensity;
	Instance.CloudShapeScaleUU = CloudShapeScaleUU;
	Instance.CloudErosion = CloudErosion;
	Instance.CloudClusterScaleUU = CloudClusterScaleUU;
	Instance.CloudMesoScaleUU = CloudMesoScaleUU;
	Instance.CloudFairWeatherDepthUU = CloudFairWeatherDepthUU;
	Instance.RaymarchSteps = RaymarchSteps;
	Instance.CloudSkyAmbientScale = CloudSkyAmbientScale;
	Instance.ScatteringUU = ScatteringUU;
	Instance.Weather = Weather;
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
