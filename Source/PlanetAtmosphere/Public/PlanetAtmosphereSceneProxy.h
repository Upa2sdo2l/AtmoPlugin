// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PrimitiveSceneProxy.h"
#include "Templates/SharedPointer.h"

class UPlanetAtmosphereComponent;
class FAtmosphereProxyRegistry;
class FRHICommandListBase;

/**
 * Scene proxy for planet atmosphere rendering.
 * Render-side representation of UPlanetAtmosphereComponent.
 *
 * Thread-safety / lifetime:
 * - Constructed on the Game Thread (UPrimitiveComponent::CreateSceneProxy); all data is
 *   copied by value here, no UObject pointers are kept.
 * - Registers itself in FAtmosphereProxyRegistry in CreateRenderThreadResources()
 *   and unregisters in DestroyRenderThreadResources() — both are invoked by the engine
 *   on the render side, so the registry never holds a pointer to a deleted proxy.
 * - Holds a thread-safe shared reference to the registry (plain C++ object),
 *   never to UAtmosphereWorldSubsystem.
 */
class FPlanetAtmosphereSceneProxy : public FPrimitiveSceneProxy
{
public:
	FPlanetAtmosphereSceneProxy(const UPlanetAtmosphereComponent* InComponent);
	virtual ~FPlanetAtmosphereSceneProxy();

	//~ Begin FPrimitiveSceneProxy Interface
	virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override;
	virtual void DestroyRenderThreadResources() override;

	virtual void GetDynamicMeshElements(
		const TArray<const FSceneView*>& Views,
		const FSceneViewFamily& ViewFamily,
		uint32 VisibilityMap,
		FMeshElementCollector& Collector) const override;

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	virtual uint32 GetMemoryFootprint() const override { return sizeof(*this); }
	virtual SIZE_T GetTypeHash() const override;
	//~ End FPrimitiveSceneProxy Interface

public:
	// Immutable render state (safe to read from Render Thread)
	const FVector3d PlanetCenterWorld;
	const double PlanetRadius;
	const double AtmosphereBottomRadius;
	const double AtmosphereTopRadius;
	const double CloudBottomRadius;
	const double CloudTopRadius;
	const float CloudCoverage;
	const float CloudDensity;
	const int32 RaymarchSteps;

private:
	/** Shared (non-UObject) registry; may be null if the world has no subsystem (e.g. preview worlds). */
	TSharedPtr<FAtmosphereProxyRegistry, ESPMode::ThreadSafe> Registry;
	bool bRegistered = false;
};
