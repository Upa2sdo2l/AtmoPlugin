// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PrimitiveSceneProxy.h"
#include "Templates/SharedPointer.h"
#include "PlanetAtmosphereTypes.h"

class UPlanetAtmosphereComponent;
class FAtmosphereProxyRegistry;
class FRHICommandListBase;

/**
 * Scene proxy for planet atmosphere rendering.
 * Render-side representation of UPlanetAtmosphereComponent.
 *
 * Thread-safety / lifetime:
 * - Constructed by UPrimitiveComponent::CreateSceneProxy(); parameters are copied by value,
 *   no UObject pointers are kept.
 * - Planet position is NOT cached here: it is read from the engine-maintained
 *   GetLocalToWorld()/GetBounds(), which the engine updates via SetTransform when the
 *   actor moves (moving does not recreate the proxy).
 * - Registers itself in FAtmosphereProxyRegistry in CreateRenderThreadResources()
 *   and unregisters in DestroyRenderThreadResources(); the engine calls the latter
 *   before deleting the proxy, so the registry never holds a pointer to a deleted proxy.
 * - The registry never hands proxy pointers out: consumers get FAtmosphereVisibleInstance copies.
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

	/** Render Thread: planet center in world space (UU), from the engine-maintained transform. */
	FVector3d GetPlanetCenterWorld() const { return GetLocalToWorld().GetOrigin(); }

	/** Render Thread: POD copy of everything a renderer needs for this atmosphere. */
	FAtmosphereVisibleInstance MakeVisibleInstance() const;

public:
	// Immutable render parameters (Unreal Units for distances).
	const FPlanetAtmosphereRadii RadiiUU;
	const float CloudCoverage;
	const float CloudDensity;
	const int32 RaymarchSteps;

private:
	/** Shared (non-UObject) registry; null if the world has no subsystem (e.g. preview worlds). */
	TSharedPtr<FAtmosphereProxyRegistry, ESPMode::ThreadSafe> Registry;
	bool bRegistered = false;
};
