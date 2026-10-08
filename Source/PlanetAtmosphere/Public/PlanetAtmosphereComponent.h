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
	// (Earth-like, Mars-like or fictional). Every coefficient is Color x Scale (as in UE SkyAtmosphere):
	// the color (0..1) is the visible tint, the scale is the strength per KILOMETER at the bottom of the atmosphere
	// (AtmosphereBottomRadius). All heights / altitudes are in meters above AtmosphereBottomRadius.

	/** Tint of Rayleigh (small molecules) scattering. Earth: blue sky, red sunsets. Coefficient = color x scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere Scattering", meta = (HideAlphaChannel))
	FLinearColor RayleighScatteringColor = FLinearColor(0.175287f, 0.409607f, 1.0f);

	/** Strength of Rayleigh scattering, per km (multiplies the color). Earth: 0.0331. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere Scattering", meta = (ClampMin = "0.0", UIMax = "0.2"))
	float RayleighScatteringScale = 0.0331f;

	/** Altitude (m) over which the Rayleigh density falls by a factor e (exponential profile). Earth: 8 km. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere Scattering", meta = (ClampMin = "1.0"))
	float RayleighScaleHeight = 8000.0f;

	/** Tint of Mie (aerosols, dust, haze) scattering: whitish haze and the glow around the sun. Coefficient = color x scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere Scattering", meta = (HideAlphaChannel))
	FLinearColor MieScatteringColor = FLinearColor(1.0f, 1.0f, 1.0f);

	/** Strength of Mie scattering, per km (multiplies the color). Earth: 0.003996. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere Scattering", meta = (ClampMin = "0.0", UIMax = "0.1"))
	float MieScatteringScale = 0.003996f;

	/** Tint of Mie absorption (aerosols absorb a little light; dust on Mars absorbs a lot of blue). Coefficient = color x scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere Scattering", meta = (HideAlphaChannel))
	FLinearColor MieAbsorptionColor = FLinearColor(1.0f, 1.0f, 1.0f);

	/** Strength of Mie absorption, per km (multiplies the color). Earth: 0.000444. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere Scattering", meta = (ClampMin = "0.0", UIMax = "0.1"))
	float MieAbsorptionScale = 0.000444f;

	/** Altitude (m) over which the Mie density falls by a factor e (exponential profile). Earth: 1.2 km. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere Scattering", meta = (ClampMin = "1.0"))
	float MieScaleHeight = 1200.0f;

	/** Mie phase anisotropy g: 0 = scatters equally in all directions, close to 1 = strong forward glow around the sun. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere Scattering", meta = (ClampMin = "0.0", ClampMax = "0.999"))
	float MieAnisotropy = 0.8f;

	/** Tint of ozone absorption (absorbs mostly green / red: deepens the blue of the zenith at twilight). Coefficient = color x scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere Scattering", meta = (HideAlphaChannel))
	FLinearColor OzoneAbsorptionColor = FLinearColor(0.345561f, 1.0f, 0.045189f);

	/** Strength of ozone absorption at the peak of the layer, per km (multiplies the color). Earth: 0.001881. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere Scattering", meta = (ClampMin = "0.0", UIMax = "0.1"))
	float OzoneAbsorptionScale = 0.001881f;

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

	/**
	 * Strength of the sky light on the clouds (Step 15). With the atmosphere on, cloud ambient light is the sky radiance
	 * from the atmosphere (multiple-scattering LUT: blue by day, colored at sunset, dark at night) x this value.
	 * TEMPORARY ARTISTIC COMPENSATION until Phase 7: the clouds have no multiple scattering inside them yet, so the
	 * physically correct value 1 makes them far too dark (from orbit ~1/3 of the Phase 2 brightness); ~5 keeps the
	 * daytime brightness close to Phase 2. Phase 7 brings it towards 1 or replaces it with a full model.
	 * Not used with r.PlanetAtmosphere.Atmosphere 0 / MultipleScattering 0 (then CloudAmbientIntensity applies).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ClampMin = "0.0", UIMax = "20.0"))
	float CloudSkyAmbientScale = 5.0f;

	// ===== RENDERING PARAMETERS =====

	/** Number of raymarch steps (higher = better quality, more expensive) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering", meta = (ClampMin = "16", ClampMax = "256"))
	int32 RaymarchSteps = 64;

	// ===== WEATHER (Phase 5 / Step 27) =====
	// Deterministic planetary weather (model C): climatology + transported synoptic noise + storm objects, a pure function
	// of (seed, weather time, these parameters, PlanetRadius). Same values -> same weather on every machine.
	// Defaults are Earth-like. Weather time: UAtmosphereWorldSubsystem (SetWeatherTime / SetWeatherTimeScale),
	// default = world time x r.PlanetAtmosphere.Weather.TimeScale (4: one 24 h day = 6 real hours).
	// Note: Earth-like bands also need an Earth-like size and rotation; the default 1000 km planet rotating in 24 h is a
	// slow rotator for its size (Hadley cells reach 60 deg). Use PlanetRadius 6,371,000 m or a faster rotation.
	// Step 27 only computes the weather (visible with r.PlanetAtmosphere.DebugMode 14); it drives the clouds from Step 29.

	/** Seed of the weather sequence (storms, noise). Same seed and parameters = same weather. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	int32 WeatherSeed = 1;

	/** Length of one rotation (planet day) in game hours. Faster rotation = narrower climate belts, more storms. Earth: 24. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "1.0", ClampMax = "2400.0", UIMax = "240.0"))
	float RotationPeriodHours = 24.0f;

	/** Retrograde rotation: the winds and the rotation sense of the storms flip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	bool bRetrogradeRotation = false;

	/** Axial tilt in degrees: how far the tropical rain belt moves with the seasons. Earth: 23.44. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float AxialTilt = 23.44f;

	/** Year length in planet days (rotations). Earth: 365.25. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "1.0", UIMax = "1000.0"))
	float YearLengthDays = 365.25f;

	/** Position in the year at weather time 0: 0 = northern spring equinox, 0.25 = northern summer, 0.5 = autumn, 0.75 = winter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SeasonPhase = 0.0f;

	/** Global mean temperature, degrees Celsius. Earth: 15. (Temperature field: Step 28 debug view.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "-250.0", ClampMax = "500.0", UIMin = "-100.0", UIMax = "100.0"))
	float MeanTemperature = 15.0f;

	/** Temperature difference between the equator and the poles, kelvin. Earth: ~40. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0.0", ClampMax = "200.0"))
	float EquatorPoleTemperatureDifference = 40.0f;

	/** Mean relative humidity at the cloud level (0 = dry, 1 = saturated): the baseline amount of cloud. Earth-like: 0.6. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MeanHumidity = 0.6f;

	/** Mean lifetime of an extratropical cyclone, planet days. Default 5; 3..7 for testing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0.5", ClampMax = "30.0", UIMin = "3.0", UIMax = "7.0"))
	float CycloneLifetimeDays = 5.0f;

	/** Extratropical cyclones alive per hemisphere on average (scaled by the rotation rate). Earth-like: 6. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0.0", ClampMax = "12.0"))
	float CyclonesPerHemisphere = 6.0f;

	/** Radius of an extratropical cyclone in meters (smaller on planets smaller than Earth). Earth-like: 1,200,000 m. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "50000.0", ClampMax = "5000000.0"))
	double CycloneRadius = 1200000.0;

	/** Tropical cyclones per hemisphere in its warm season. Earth-like: 1.5. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float TropicalCyclonesPerHemisphere = 1.5f;

	/** Multiplier of all wind speeds (zonal flow, storm drift, storm winds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float WindScale = 1.0f;

	/** Weather parameters with runtime-safe clamps (UPROPERTY clamps only apply to Details-panel edits), model units. */
	FPlanetWeatherParameters GetValidatedWeatherParameters() const;
};
