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

	/**
	 * Atmosphere scattering parameters converted to render units (1/cm, cm) with runtime-safe clamps
	 * (UPROPERTY ClampMin/Max only applies to Details-panel edits).
	 */
	FPlanetAtmosphereScattering GetValidatedScatteringUU() const;

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

	// ===== ATMOSPHERE SCATTERING (Phase 2.5 / Step 13) =====
	// Defaults are Earth's (same values as UE SkyAtmosphere), but nothing is hardcoded: any combination is valid
	// (Earth-like, Mars-like or fictional). Coefficients are per KILOMETER at the bottom of the atmosphere
	// (AtmosphereBottomRadius); all heights / altitudes are in meters above AtmosphereBottomRadius.

	/** Rayleigh (small molecules) scattering coefficient per km for R, G, B. Sets the sky color: blue sky, red sunsets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere Scattering", meta = (HideAlphaChannel))
	FLinearColor RayleighScattering = FLinearColor(0.005802f, 0.013558f, 0.0331f);

	/** Altitude (m) over which the Rayleigh density falls by a factor e (exponential profile). Earth: 8 km. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere Scattering", meta = (ClampMin = "1.0"))
	float RayleighScaleHeight = 8000.0f;

	/** Mie (aerosols, dust, haze) scattering coefficient per km for R, G, B. Whitish haze and the glow around the sun. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere Scattering", meta = (HideAlphaChannel))
	FLinearColor MieScattering = FLinearColor(0.003996f, 0.003996f, 0.003996f);

	/** Mie absorption coefficient per km for R, G, B (aerosols absorb a little light; dust on Mars absorbs a lot). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere Scattering", meta = (HideAlphaChannel))
	FLinearColor MieAbsorption = FLinearColor(0.000444f, 0.000444f, 0.000444f);

	/** Altitude (m) over which the Mie density falls by a factor e (exponential profile). Earth: 1.2 km. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere Scattering", meta = (ClampMin = "1.0"))
	float MieScaleHeight = 1200.0f;

	/** Mie phase anisotropy g: 0 = scatters equally in all directions, close to 1 = strong forward glow around the sun. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere Scattering", meta = (ClampMin = "0.0", ClampMax = "0.999"))
	float MieAnisotropy = 0.8f;

	/** Ozone absorption coefficient per km for R, G, B at the peak of the ozone layer (deepens the blue of the zenith at twilight). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere Scattering", meta = (HideAlphaChannel))
	FLinearColor OzoneAbsorption = FLinearColor(0.000650f, 0.001881f, 0.000085f);

	/** Altitude (m) of the peak of the ozone layer (tent profile). Earth: 25 km. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere Scattering", meta = (ClampMin = "0.0"))
	float OzoneLayerAltitude = 25000.0f;

	/** Full width (m) of the ozone layer: the density falls linearly from 1 at the peak to 0 at +-width/2. Earth: 30 km. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere Scattering", meta = (ClampMin = "1.0"))
	float OzoneLayerWidth = 30000.0f;

	/** Average albedo of the planet surface: color of the debug placeholder surface; ground bounce light in Step 14. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere Scattering", meta = (HideAlphaChannel, ClampMin = "0.0", ClampMax = "1.0"))
	FLinearColor SurfaceAlbedo = FLinearColor(0.04f, 0.06f, 0.09f);

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

	/** Base cloud density (scales the extinction coefficient; 1 = typical cumulus) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float CloudDensity = 1.0f;

	/** Size of the base cloud formations in meters (larger = bigger, smoother clouds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ClampMin = "100.0", ClampMax = "1000000.0"))
	double CloudShapeScale = 8000.0;

	/** How strongly high-frequency noise erodes cloud edges (0 = smooth blobs, 1 = ragged edges) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CloudErosion = 0.5f;

	// ===== RENDERING PARAMETERS =====

	/** Number of raymarch steps (higher = better quality, more expensive) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering", meta = (ClampMin = "16", ClampMax = "256"))
	int32 RaymarchSteps = 64;
};
