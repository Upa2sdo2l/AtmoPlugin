// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

class FPlanetAtmosphereSceneProxy;
class FSceneView;

/**
 * Render-side registry of atmosphere scene proxies for one UWorld.
 *
 * NOT a UObject: it is owned via TSharedPtr<..., ThreadSafe> by both
 * UAtmosphereWorldSubsystem (Game Thread) and every FPlanetAtmosphereSceneProxy.
 * Therefore it stays alive as long as any proxy references it, even if the
 * subsystem is already deinitialized / garbage collected.
 *
 * Add/Remove are called from FPlanetAtmosphereSceneProxy::CreateRenderThreadResources /
 * DestroyRenderThreadResources (render side). Access is guarded by a lock, so it is
 * also safe if the engine calls those from a render worker task.
 */
class PLANETATMOSPHERE_API FAtmosphereProxyRegistry
{
public:
	void Add(FPlanetAtmosphereSceneProxy* Proxy)
	{
		FScopeLock Lock(&Mutex);
		Proxies.AddUnique(Proxy);
		UE_LOG(LogTemp, Log, TEXT("AtmosphereProxyRegistry: Registered proxy (Total: %d)"), Proxies.Num());
	}

	void Remove(FPlanetAtmosphereSceneProxy* Proxy)
	{
		FScopeLock Lock(&Mutex);
		Proxies.RemoveSingleSwap(Proxy);
		UE_LOG(LogTemp, Log, TEXT("AtmosphereProxyRegistry: Unregistered proxy (Remaining: %d)"), Proxies.Num());
	}

	/**
	 * Render Thread only. Returned pointers are valid for the duration of the
	 * current render frame (proxies are destroyed on the render side between frames).
	 */
	TArray<FPlanetAtmosphereSceneProxy*> GetVisibleProxies(const FSceneView& View) const;

	int32 Num() const
	{
		FScopeLock Lock(&Mutex);
		return Proxies.Num();
	}

private:
	mutable FCriticalSection Mutex;
	TArray<FPlanetAtmosphereSceneProxy*> Proxies;
};
