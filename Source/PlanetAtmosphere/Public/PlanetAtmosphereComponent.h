// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "PlanetAtmosphereTypes.h"
#include "PlanetAtmosphereComponent.generated.h"

/**
 * Component that defines a planet's atmosphere parameters.
 * This is the Game Thread interface for atmosphere configuration.
 *
 * Units: all radii are in METERS (converted to Unreal Units = cm internally).
 * The atmosphere is centered on the component location; component SCALE IS IGNORED
 * (radii are absolute), so bounds and render data always match the values below.
 *
 * Registers itself in UAtmosphereWorldSubsystem in OnRegister/OnUnregister,
 * so it is tracked identically in Editor and in Game/PIE.
 */
UCLASS(ClassGroup=(Rendering), meta=(BlueprintSpawnableComponent))
class PLANETATMOSPHERE_API UPlanetAtmosphereComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UPlanetAtmosphereComponent();

	//~ Begin UPrimitiveComponent Interface
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End UPrimitiveComponent Interface

	//~ Begin UObject Interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject Interface

	/**
	 * Radii in meters with the shell invariants enforced (works at runtime too,
	 * PostEditChangeProperty clamps only exist in the editor).
	 */
	FPlanetAtmosphereRadii GetValidatedRadiiMeters() const;

	/** Same as GetValidatedRadiiMeters(), converted to Unreal Units (cm). */
	FPlanetAtmosphereRadii GetValidatedRadiiUU() const
	{
		return GetValidatedRadiiMeters().Scaled(PlanetAtmosphere::MetersToUnrealUnits);
	}

protected:
	//~ Begin UActorComponent Interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	//~ End UActorComponent Interface

public:
	// ===== PLANET PARAMETERS =====

	/** Radius of the planet surface in meters (1,000,000 m = 1000 km; Earth = 6,371,000 m) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "1000.0"))
	double PlanetRadius = 1000000.0;

	// ===== ATMOSPHERE PARAMETERS =====

	/** Bottom of the atmosphere in meters (typically same as planet radius) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere", meta = (ClampMin = "1000.0"))
	double AtmosphereBottomRadius = 1000000.0;

	/** Top of the atmosphere in meters */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere", meta = (ClampMin = "1000.0"))
	double AtmosphereTopRadius = 1100000.0;

	// ===== CLOUD PARAMETERS =====

	/** Bottom of the cloud layer in meters */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ClampMin = "1000.0"))
	double CloudBottomRadius = 1010000.0;

	/** Top of the cloud layer in meters */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ClampMin = "1000.0"))
	double CloudTopRadius = 1020000.0;

	/** Cloud coverage amount (0 = no clouds, 1 = full coverage) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CloudCoverage = 0.5f;

	/** Base cloud density */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float CloudDensity = 1.0f;

	// ===== RENDERING PARAMETERS =====

	/** Number of raymarch steps (higher = better quality, more expensive) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering", meta = (ClampMin = "16", ClampMax = "256"))
	int32 RaymarchSteps = 64;
};
