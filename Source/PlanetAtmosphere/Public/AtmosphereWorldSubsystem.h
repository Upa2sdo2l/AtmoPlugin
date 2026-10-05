// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/SharedPointer.h"
#include "AtmosphereWorldSubsystem.generated.h"

class UPlanetAtmosphereComponent;
class FAtmosphereProxyRegistry;
class FPlanetAtmosphereViewExtension;

/**
 * World Subsystem that manages all planet atmospheres in the level.
 *
 * Game Thread only. The render side never touches this UObject:
 * it works exclusively with FAtmosphereProxyRegistry (plain C++, shared-ptr owned).
 *
 * Ownership (one set per world):
 *   Subsystem ==> ProxyRegistry            (TSharedPtr, ThreadSafe)
 *   Subsystem ==> ViewExtension            (TSharedPtr, ThreadSafe; the engine keeps only a weak ref)
 *   ViewExtension ==> ProxyRegistry
 *   SceneProxy    ==> ProxyRegistry
 * The ViewExtension is an FWorldSceneViewExtension bound to this world, so it is inactive
 * for view families of other worlds (e.g. editor world vs PIE, asset preview scenes).
 */
UCLASS()
class PLANETATMOSPHERE_API UAtmosphereWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem Interface

	/** Register an atmosphere component - Game Thread (called from UPlanetAtmosphereComponent::OnRegister) */
	void RegisterAtmosphere(UPlanetAtmosphereComponent* Component);

	/** Unregister an atmosphere component - Game Thread (called from UPlanetAtmosphereComponent::OnUnregister) */
	void UnregisterAtmosphere(UPlanetAtmosphereComponent* Component);

	/** All registered atmosphere components - Game Thread */
	const TArray<TWeakObjectPtr<UPlanetAtmosphereComponent>>& GetRegisteredAtmospheres() const
	{
		return RegisteredAtmospheres;
	}

	/** Shared render-side registry. Copied into each scene proxy on construction. */
	TSharedPtr<FAtmosphereProxyRegistry, ESPMode::ThreadSafe> GetProxyRegistry() const
	{
		return ProxyRegistry;
	}

private:
	/** All registered atmosphere components - Game Thread ONLY */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UPlanetAtmosphereComponent>> RegisteredAtmospheres;

	/** Render-side proxy registry (not a UObject, thread-safe ref-counted) */
	TSharedPtr<FAtmosphereProxyRegistry, ESPMode::ThreadSafe> ProxyRegistry;

	/** This world's view extension. Null when the process can never render (e.g. dedicated server). */
	TSharedPtr<FPlanetAtmosphereViewExtension, ESPMode::ThreadSafe> ViewExtension;
};
