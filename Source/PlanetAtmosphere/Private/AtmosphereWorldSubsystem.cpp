// Copyright Epic Games, Inc. All Rights Reserved.

#include "AtmosphereWorldSubsystem.h"
#include "AtmosphereProxyRegistry.h"
#include "PlanetAtmosphereComponent.h"
#include "PlanetAtmosphereViewExtension.h"
#include "PlanetAtmosphereTypes.h"
#include "SceneViewExtension.h"
#include "Engine/World.h"
#include "Misc/App.h"

void UAtmosphereWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UWorld* World = GetWorld();
	ProxyRegistry = MakeShared<FAtmosphereProxyRegistry, ESPMode::ThreadSafe>();

	if (FApp::CanEverRender())
	{
		// FWorldSceneViewExtension(const FAutoRegister&, UWorld*) — the FAutoRegister argument
		// is supplied by NewExtension, the remaining arguments are forwarded to our constructor.
		ViewExtension = FSceneViewExtensions::NewExtension<FPlanetAtmosphereViewExtension>(World, ProxyRegistry);
	}

	UE_LOG(LogPlanetAtmosphere, Log, TEXT("AtmosphereWorldSubsystem: Initialized for world '%s' (ViewExtension: %s)"),
		*GetNameSafe(World), ViewExtension.IsValid() ? TEXT("yes") : TEXT("no"));
}

void UAtmosphereWorldSubsystem::Deinitialize()
{
	UE_LOG(LogPlanetAtmosphere, Log, TEXT("AtmosphereWorldSubsystem: Deinitializing world '%s'"), *GetNameSafe(GetWorld()));

	RegisteredAtmospheres.Empty();

	// The engine holds only weak references to view extensions; releasing ours destroys it
	// (immediately, or right after the frame in which the render thread temporarily pinned it).
	ViewExtension.Reset();

	// Proxies still alive (until the render thread destroys them) keep the registry alive via their own refs.
	ProxyRegistry.Reset();

	Super::Deinitialize();
}

void UAtmosphereWorldSubsystem::RegisterAtmosphere(UPlanetAtmosphereComponent* Component)
{
	if (Component && !RegisteredAtmospheres.Contains(Component))
	{
		RegisteredAtmospheres.Add(Component);
		UE_LOG(LogPlanetAtmosphere, Log, TEXT("AtmosphereWorldSubsystem: Registered atmosphere '%s' (Total: %d)"),
			*GetPathNameSafe(Component), RegisteredAtmospheres.Num());
	}
}

void UAtmosphereWorldSubsystem::UnregisterAtmosphere(UPlanetAtmosphereComponent* Component)
{
	if (Component)
	{
		// Also drops entries whose component was already garbage collected.
		RegisteredAtmospheres.RemoveAll([Component](const TWeakObjectPtr<UPlanetAtmosphereComponent>& Entry)
		{
			return !Entry.IsValid() || Entry.Get() == Component;
		});
		UE_LOG(LogPlanetAtmosphere, Log, TEXT("AtmosphereWorldSubsystem: Unregistered atmosphere '%s' (Remaining: %d)"),
			*GetPathNameSafe(Component), RegisteredAtmospheres.Num());
	}
}
