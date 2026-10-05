// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "PlanetAtmosphereTypes.h"

class FPlanetAtmosphereSceneProxy;
class FSceneView;

/**
 * Render-side registry of atmosphere scene proxies for one UWorld.
 *
 * NOT a UObject: it is owned via TSharedPtr<..., ThreadSafe> by UAtmosphereWorldSubsystem,
 * by that world's FPlanetAtmosphereViewExtension and by every FPlanetAtmosphereSceneProxy.
 * It therefore stays alive as long as any of them references it.
 *
 * Add/Remove are called from FPlanetAtmosphereSceneProxy::CreateRenderThreadResources /
 * DestroyRenderThreadResources (render side). All access is guarded by a lock.
 *
 * The stored proxy pointers NEVER leave the lock: consumers receive POD copies
 * (FAtmosphereVisibleInstance), so nothing downstream can outlive a proxy.
 */
class PLANETATMOSPHERE_API FAtmosphereProxyRegistry
{
public:
	void Add(FPlanetAtmosphereSceneProxy* Proxy);
	void Remove(FPlanetAtmosphereSceneProxy* Proxy);

	/**
	 * Render Thread. PLUGIN-SIDE culling (this is NOT the UE renderer's visibility result,
	 * which is private to the Renderer module and computed later in the frame):
	 *   1. FPrimitiveSceneProxy::IsShown(View)  - hidden-in-game/editor, hidden actors, owner flags;
	 *   2. View.ViewFrustum.IntersectSphere()  - frustum test of the proxy's world bounds
	 *      (atmosphere top sphere, UU, kept up to date by the engine when the actor moves).
	 * Distance alone never culls: a planet in front of the camera stays visible at any range.
	 * Appends copies to OutInstances; returns the number of registered proxies (before culling).
	 */
	int32 GatherVisibleInstances(const FSceneView& View, TArray<FAtmosphereVisibleInstance>& OutInstances) const;

	int32 Num() const;

private:
	mutable FCriticalSection Mutex;
	TArray<FPlanetAtmosphereSceneProxy*> Proxies;
};
