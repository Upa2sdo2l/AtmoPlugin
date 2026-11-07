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

	/**
	 * Bottom of the cloud layer in meters (radius): the base of the fair-weather clouds. Default: 2.5 km above the default
	 * planet (Step 32; Earth-like cumulus bases, reference 3 of visual-target.md).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ClampMin = "1000.0"))
	double CloudBottomRadius = 1002500.0;

	/** Top of the cloud layer in meters (radius): room for the deepest (storm) clouds. Default: 10 km above the default planet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ClampMin = "1000.0"))
	double CloudTopRadius = 1010000.0;

	/**
	 * Cloud amount. With planetary weather (r.PlanetAtmosphere.Weather 1) it scales the weather's local cloud water:
	 * 0.5 = the weather as it is (~55 % of an Earth-like planet clouded), below = drier, above = more overcast (0.65 gives
	 * ~85 %); 0 and 1 = no clouds / overcast everywhere. Without weather it is the cloud fraction everywhere.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CloudCoverage = 0.5f;

	/** Cloud density (scales the extinction coefficient 0.04 / m; Step 32 default 0.4: soft, white fair-weather cumulus) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float CloudDensity = 0.4f;

	/** Size of the individual clouds in meters (Step 32c default 1400 m, was 700; billows are 1/4 of it) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ClampMin = "100.0", ClampMax = "1000000.0"))
	double CloudShapeScale = 1400.0;

	/** How strongly the billows (Worley detail) eat into the cloud edges (0 = smooth domes, 1 = billowy / wispy edges) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CloudErosion = 1.0f;

	/**
	 * Step 32: size in meters of the cloud clusters (groups of clouds and the gaps between them, organized by the weather).
	 * Seen from orbit they are the texture inside the weather zones; from the surface they decide where a field of clouds is.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", AdvancedDisplay, meta = (ClampMin = "1000.0", ClampMax = "10000000.0"))
	double CloudClusterScale = 100000.0;

	/** Step 32: size in meters of the meso cells (open / closed cells inside cloud decks, relief of the tops from low orbit). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", AdvancedDisplay, meta = (ClampMin = "100.0", ClampMax = "1000000.0"))
	double CloudMesoScale = 12000.0;

	/**
	 * Step 32: depth in meters of the fair-weather clouds (flat bases at the cloud bottom); storms grow up to the cloud top.
	 * Step 32b default 800 m (was 600); Step 32c 1200 m with the 1400 m clouds (puffy cumulus, not flat "paper").
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ClampMin = "50.0", ClampMax = "100000.0"))
	double CloudFairWeatherDepth = 1200.0;

	/**
	 * Step 37b: size in meters of the macro clouds -- the same cloud shape (cell noise) as the individual clouds, at this
	 * size and at 1/4 of it. They gather the clouds into big masses with gaps between them and raise their tops, so that
	 * the clouds read as volumes from high orbit instead of a flat plate. Applies wherever the weather has clouds.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Relief", meta = (ClampMin = "1000.0", ClampMax = "1000000.0"))
	double CloudHeapScale = 48000.0;

	/**
	 * Step 37b: height of the macro clouds relative to their size (1 = 8 % of the size for the large ones, 12 % for the
	 * small ones, before the vertical exaggeration). 0 = no macro clouds (flat tops of Step 36).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Relief", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float CloudHeapAmplitude = 1.0f;

	/** Step 37b: share of the macro cloud height where the weather has few clouds (1 where it has many). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Relief", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CloudHeapFairWeather = 0.7f;

	/**
	 * Step 37: art-directed vertical exaggeration of the clouds seen from far away. Between Exaggeration Start Altitude and
	 * Exaggeration Full Altitude of the camera the cloud layer and the fair-weather depth grow smoothly from x1 (physical,
	 * near the planet) to x this, so that the relief of the cloud masses is readable from orbit. 1 = off.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Relief", meta = (ClampMin = "1.0", UIMax = "10.0"))
	float CloudVerticalExaggeration = 5.0f;

	/** Step 37: camera altitude in meters below which the clouds are not exaggerated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Relief", meta = (ClampMin = "100.0"))
	double CloudExaggerationStartAltitude = 30000.0;

	/** Step 37: camera altitude in meters from which the full exaggeration applies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Relief", meta = (ClampMin = "200.0"))
	double CloudExaggerationFullAltitude = 600000.0;

	/**
	 * Strength of the sky light on the clouds (Step 15). With the atmosphere on, cloud ambient light is the sky radiance
	 * from the atmosphere (multiple-scattering LUT: blue by day, colored at sunset, dark at night) x this value.
	 * TEMPORARY ARTISTIC COMPENSATION until Phase 7: the clouds have no multiple scattering inside them yet, so the
	 * physically correct value 1 makes them far too dark (from orbit ~1/3 of the Phase 2 brightness); ~5 kept the
	 * daytime brightness close to Phase 2. Step 32c (r.PlanetAtmosphere.CloudLightingModel 1) adds multiple-scattering
	 * octaves to the sun term, so the default is 2: x5 then filled the clouds with one flat sky tone. Phase 7 brings it
	 * towards 1 or replaces it with a full model.
	 * Not used with r.PlanetAtmosphere.Atmosphere 0 / MultipleScattering 0 (then CloudAmbientIntensity applies).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ClampMin = "0.0", UIMax = "20.0"))
	float CloudSkyAmbientScale = 2.0f;

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

	// ===== Step 38: flowmap weather (r.PlanetAtmosphere.Weather.FlowWarp 2, the default) =====
	// No meteorology: a multi-octave cloud pattern, distorted (hooks, commas), gathered into tufts where the cover is
	// partial, wound into broad comma shields around the storms above (streaks and thin slits inside them), carried east by
	// the wind bands; the cloud amount follows a latitude profile. The settings above still place and move the storms and
	// the climate belts. Sizes are for an Earth-size planet and scale with PlanetRadius (2500 km: x 0.39).

	/** Trade-wind speed (m/s): easterlies of the tropical cells. Moves and orients the cloud pattern. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float FlowTradeWind = 8.0f;

	/** Mid-latitude westerly speed (m/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "0.0", ClampMax = "80.0"))
	float FlowJetWind = 22.0f;

	/** Polar easterly speed (m/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "0.0", ClampMax = "40.0"))
	float FlowPolarWind = 6.0f;

	/** Meridional wind (m/s): toward the equator in the tropics, poleward in mid latitudes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float FlowMeridionalWind = 4.0f;

	/** Size of the large distortion of the cloud pattern (hooks, commas), meters on an Earth-size planet (scaled with the radius). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "100000.0", ClampMax = "20000000.0"))
	double FlowWarpScale = 2039000.0;

	/** How far the large distortion moves the pattern, meters on an Earth-size planet. More = more curled, marbled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "0.0", ClampMax = "5000000.0"))
	double FlowWarpDisplacement = 153000.0;

	/** Size of the fine distortion, meters on an Earth-size planet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "50000.0", ClampMax = "10000000.0"))
	double FlowFineWarpScale = 637000.0;

	/** How far the fine distortion moves the pattern, meters on an Earth-size planet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "0.0", ClampMax = "2000000.0"))
	double FlowFineWarpDisplacement = 64000.0;

	/** Largest size of the cloud pattern, meters on an Earth-size planet; finer octaves at 1/2, 1/4, 1/8, 1/16. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "200000.0", ClampMax = "20000000.0"))
	double FlowNoiseScale = 3058000.0;

	/** Sharpness of the cloud-pattern edges (lower = softer). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float FlowEdgeSharpness = 0.5f;

	/** Size of the tufts / rounded clear holes where the cover is partial, meters on an Earth-size planet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "50000.0", ClampMax = "10000000.0"))
	double FlowClumpScale = 765000.0;

	/** How strongly partial cloud gathers into tufts around rounded holes (0 = off). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float FlowClumpStrength = 1.0f;

	/** Cloud fraction of the tropical rain belt (ITCZ). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FlowCloudItcz = 0.4f;

	/** Cloud fraction of the subtropics (mostly clear with tufts). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FlowCloudSubtropics = 0.25f;

	/** Cloud fraction of the mid latitudes (storm tracks). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FlowCloudMidLatitudes = 0.5f;

	/** Cloud fraction of the polar regions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FlowCloudPoles = 0.35f;

	/** Size of a storm's cloud shield relative to Cyclone Radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "0.5", ClampMax = "8.0"))
	float FlowStormShieldRatio = 3.5f;

	/** Maximum winding of the pattern around a storm, degrees (about 50 = a broad comma, 360 = a full turn). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float FlowStormRotation = 51.6f;

	/** Cloud fraction added inside a storm shield. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FlowStormCloud = 0.5f;

	/** Deep (storm) clouds in the tropical rain belt, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FlowItczStorminess = 0.6f;

	/** Size of the slow large variation of the cloud amount (where shields and clear regions are), meters on an Earth-size planet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "500000.0", ClampMax = "40000000.0"))
	double FlowSynopticScale = 7136000.0;

	/** Strength of that variation (cloud fraction units). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FlowSynopticStrength = 0.35f;

	/** Length of the streaks along the flow inside storm shields, meters on an Earth-size planet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "0.0", ClampMax = "5000000.0"))
	double FlowStreakLength = 1274000.0;

	/** Strength of the thin clear slits along the flow inside storm shields. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float FlowSlitStrength = 0.6f;

	/**
	 * Step 39: latitude (degrees) of the edge of the tropical cells for the flowmap weather; the mid-latitude / polar edge
	 * is halfway to the pole. Storm tracks, wind bands and the cloud profile follow it. 0 = derived from the rotation
	 * period and the radius as in the physical model (a slowly rotating small planet then gets tropics up to 60 deg).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Flowmap", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float FlowTropicsEdge = 30.0f;

	/** Weather parameters with runtime-safe clamps (UPROPERTY clamps only apply to Details-panel edits), model units. */
	FPlanetWeatherParameters GetValidatedWeatherParameters() const;
};
