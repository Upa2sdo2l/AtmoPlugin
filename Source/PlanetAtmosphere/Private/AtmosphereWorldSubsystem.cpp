// Copyright Epic Games, Inc. All Rights Reserved.

#include "AtmosphereWorldSubsystem.h"
#include "AtmosphereProxyRegistry.h"
#include "PlanetAtmosphereActor.h"
#include "PlanetAtmosphereSceneProxy.h"
#include "PlanetAtmosphereModule.h"
#include "SceneView.h"
#include "Modules/ModuleManager.h"

TArray<FPlanetAtmosphereSceneProxy*> FAtmosphereProxyRegistry::GetVisibleProxies(const FSceneView& View) const
{
	TArray<FPlanetAtmosphereSceneProxy*> Result;
	FScopeLock Lock(&Mutex);
	for (FPlanetAtmosphereSceneProxy* Proxy : Proxies)
	{
		if (Proxy && Proxy->IsShown(&View))
		{
			Result.Add(Proxy);
		}
	}
	return Result;
}

void UAtmosphereWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ProxyRegistry = MakeShared<FAtmosphereProxyRegistry, ESPMode::ThreadSafe>();

	FPlanetAtmosphereModule& Module = FModuleManager::LoadModuleChecked<FPlanetAtmosphereModule>("PlanetAtmosphere");
	Module.RegisterViewExtensionForWorld(GetWorld(), ProxyRegistry);

	UE_LOG(LogTemp, Log, TEXT("AtmosphereWorldSubsystem: Initialized with ViewExtension"));
}

void UAtmosphereWorldSubsystem::Deinitialize()
{
	RegisteredAtmospheres.Empty();

	if (FModuleManager::Get().IsModuleLoaded("PlanetAtmosphere"))
	{
		FPlanetAtmosphereModule& Module = FModuleManager::GetModuleChecked<FPlanetAtmosphereModule>("PlanetAtmosphere");
		Module.UnregisterViewExtensionForWorld(GetWorld());
	}

	ProxyRegistry.Reset();
	UE_LOG(LogTemp, Log, TEXT("AtmosphereWorldSubsystem: Deinitialized"));
	Super::Deinitialize();
}

void UAtmosphereWorldSubsystem::RegisterAtmosphere(APlanetAtmosphereActor* Actor)
{
	if (Actor && !RegisteredAtmospheres.Contains(Actor))
	{
		RegisteredAtmospheres.Add(Actor);
		UE_LOG(LogTemp, Log, TEXT("AtmosphereWorldSubsystem: Registered atmosphere '%s' (Total: %d)"),
			*Actor->GetName(), RegisteredAtmospheres.Num());
	}
}

void UAtmosphereWorldSubsystem::UnregisterAtmosphere(APlanetAtmosphereActor* Actor)
{
	if (Actor)
	{
		RegisteredAtmospheres.Remove(Actor);
		UE_LOG(LogTemp, Log, TEXT("AtmosphereWorldSubsystem: Unregistered atmosphere '%s' (Remaining: %d)"),
			*Actor->GetName(), RegisteredAtmospheres.Num());
	}
}
