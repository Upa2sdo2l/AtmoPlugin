// Copyright Epic Games, Inc. All Rights Reserved.

#include "AtmosphereProxyRegistry.h"
#include "PlanetAtmosphereSceneProxy.h"
#include "Misc/ScopeLock.h"
#include "SceneView.h"

void FAtmosphereProxyRegistry::Add(FPlanetAtmosphereSceneProxy* Proxy)
{
	FScopeLock Lock(&Mutex);
	Proxies.AddUnique(Proxy);
	UE_LOG(LogPlanetAtmosphere, Log, TEXT("ProxyRegistry %p: registered proxy (Total: %d)"), static_cast<const void*>(this), Proxies.Num());
}

void FAtmosphereProxyRegistry::Remove(FPlanetAtmosphereSceneProxy* Proxy)
{
	FScopeLock Lock(&Mutex);
	Proxies.RemoveSingleSwap(Proxy);
	UE_LOG(LogPlanetAtmosphere, Log, TEXT("ProxyRegistry %p: unregistered proxy (Remaining: %d)"), static_cast<const void*>(this), Proxies.Num());
}

int32 FAtmosphereProxyRegistry::Num() const
{
	FScopeLock Lock(&Mutex);
	return Proxies.Num();
}

int32 FAtmosphereProxyRegistry::GatherVisibleInstances(const FSceneView& View, TArray<FAtmosphereVisibleInstance>& OutInstances) const
{
	FScopeLock Lock(&Mutex);

	for (const FPlanetAtmosphereSceneProxy* Proxy : Proxies)
	{
		if (!Proxy || !Proxy->IsShown(&View))
		{
			continue;
		}

		// World-space bounds from CalcBounds (atmosphere top sphere, UU), updated by the engine on transform change.
		const FBoxSphereBounds& Bounds = Proxy->GetBounds();

		// Explicit float cast: the documented 5.6 overload takes the radius as float.
		// Precision is irrelevant for a conservative culling test.
		if (!View.ViewFrustum.IntersectSphere(Bounds.Origin, static_cast<float>(Bounds.SphereRadius)))
		{
			continue;
		}

		OutInstances.Add(Proxy->MakeVisibleInstance());
	}

	return Proxies.Num();
}
