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

	// ===== Weather clock (Phase 5 / Step 27) =====
	// One clock per world, in game seconds; the weather of every planet is a pure function of it (and of the planet's
	// parameters), so clients that agree on the weather time see the same weather.
	// Default: world time (UWorld::GetTimeSeconds: pauses with the game, follows time dilation) x the time scale
	// (r.PlanetAtmosphere.Weather.TimeScale, default 4 = one 24 h game day in 6 real hours).
	// Multiplayer / save games: call SetWeatherTime with the authoritative value (e.g. replicated from the server); the
	// clock then keeps running from it at the time scale. r.PlanetAtmosphere.Weather.TimeOffsetHours is added on top (testing).

	/** Sets the weather time (game seconds) now; the clock keeps running from it at the weather time scale. */
	UFUNCTION(BlueprintCallable, Category = "Planet Atmosphere|Weather")
	void SetWeatherTime(double WeatherTimeSeconds);

	/** Game seconds of weather per world second (0 = frozen). Continuous: the weather time does not jump. Overrides the CVar. */
	UFUNCTION(BlueprintCallable, Category = "Planet Atmosphere|Weather")
	void SetWeatherTimeScale(double GameSecondsPerWorldSecond);

	/** Back to the default clock (world time x r.PlanetAtmosphere.Weather.TimeScale); the weather time may jump. */
	UFUNCTION(BlueprintCallable, Category = "Planet Atmosphere|Weather")
	void ClearWeatherTimeOverride();

	/** Current weather time in game seconds (what the renderer uses this frame, incl. TimeOffsetHours). */
	UFUNCTION(BlueprintPure, Category = "Planet Atmosphere|Weather")
	double GetWeatherTime() const;

	/** Current weather time scale (SetWeatherTimeScale, else r.PlanetAtmosphere.Weather.TimeScale). */
	UFUNCTION(BlueprintPure, Category = "Planet Atmosphere|Weather")
	double GetWeatherTimeScale() const;

private:
	/** All registered atmosphere components - Game Thread ONLY */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UPlanetAtmosphereComponent>> RegisteredAtmospheres;

	/** Render-side proxy registry (not a UObject, thread-safe ref-counted) */
	TSharedPtr<FAtmosphereProxyRegistry, ESPMode::ThreadSafe> ProxyRegistry;

	/** This world's view extension. Null when the process can never render (e.g. dedicated server). */
	TSharedPtr<FPlanetAtmosphereViewExtension, ESPMode::ThreadSafe> ViewExtension;

	/** Weather time without the CVar offset (game seconds). */
	double GetWeatherBaseTime() const;

	// Weather clock state: when anchored, weather time = AnchorTime + (world time - AnchorWorldTime) x scale.
	bool bWeatherTimeAnchored = false;
	double WeatherAnchorTime = 0.0;
	double WeatherAnchorWorldTime = 0.0;
	bool bWeatherTimeScaleOverridden = false;
	double WeatherTimeScaleOverride = 1.0;
};
