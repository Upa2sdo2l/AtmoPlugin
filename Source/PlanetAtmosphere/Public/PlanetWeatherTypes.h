// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Validated planet weather parameters (Phase 5 / Step 27, decision AD-30, model C of prototype p26).
 * Built by UPlanetAtmosphereComponent::GetValidatedWeatherParameters(), copied by value into the scene proxy and the
 * per-view FAtmosphereVisibleInstance. Plain data only (no UObject, no engine types): the weather model
 * (AtmosphereWeatherModel.h) depends on nothing else.
 *
 * Units are the ones of the model: km, game hours, m/s, kelvin. "Days" are the planet's own rotation periods.
 * The weather is a pure function of (direction on the planet, weather time, these parameters, planet radius): the same
 * inputs give the same weather on every machine (future multiplayer).
 */
struct FPlanetWeatherParameters
{
	/** Seed of the deterministic weather sequence (storms, noise anchors). */
	int32 Seed = 1;

	/** Sidereal rotation period (one planet day), game hours. Drives the cell widths (Hadley edge) and storm activity. */
	double RotationPeriodHours = 24.0;

	/** Rotation direction: false = prograde (Earth), true = retrograde (winds and storm rotation flip). */
	bool bRetrograde = false;

	/** Axial tilt, degrees: seasonal shift of the thermal equator / ITCZ. */
	double AxialTiltDegrees = 23.44;

	/** Year length in planet days. */
	double YearLengthDays = 365.25;

	/** Position in the year at weather time 0 (0 = northern spring equinox, 0.25 = northern summer solstice). */
	double SeasonPhase = 0.0;

	/** Global mean temperature (K) and equator - pole difference (K). Zonal temperature only (Step 28 debug view). */
	double MeanTemperatureK = 288.15;
	double EquatorPoleDifferenceK = 40.0;

	/** Mean relative humidity at the cloud level (climatology baseline, 0..1). */
	double Humidity = 0.6;

	/** Mean lifetime of an extratropical cyclone, planet days (prototype default 5, test range 3..7). */
	double CycloneLifetimeDays = 5.0;

	/** Extratropical cyclones alive per hemisphere on average (scaled by the rotation rate). */
	double CyclonesPerHemisphere = 6.0;

	/** Radius of an extratropical cyclone, km (scaled down on planets smaller than Earth). */
	double CycloneRadiusKm = 1200.0;

	/** Tropical cyclones per hemisphere in its warm season. */
	double TropicalCyclonesPerHemisphere = 1.5;

	/** Multiplier of all wind speeds (zonal flow, storm drift and storm winds). */
	double WindScale = 1.0;

	// ---- Step 38: flowmap weather (r.PlanetAtmosphere.Weather.FlowWarp 2). Lengths and times for an Earth-size planet,
	// scaled with PlanetRadius / 6371 km by the shader (FlowmapWeather.ush). See UPlanetAtmosphereComponent for the meaning.
	double FlowTradeWind = 8.0;          // m/s, easterlies of the Hadley cells
	double FlowJetWind = 22.0;           // m/s, westerlies of the mid latitudes
	double FlowPolarWind = 6.0;          // m/s, polar easterlies
	double FlowMeridionalWind = 4.0;     // m/s, toward the equator in the Hadley cells, poleward in the Ferrel cells
	double FlowEddyWind = 6.0;           // m/s, curl-noise eddies
	double FlowEddyScaleKm = 1800.0;
	double FlowAdvectionHours = 100.0;   // how long the noise is carried by the flow (winding, stretching)
	double FlowStreakShare = 0.375;      // part of the trace averaged into streaks (0..1)
	double FlowNoiseScaleKm = 1200.0;    // largest cloud-pattern scale (+ 0.37 x and 0.13 x of it)
	double FlowNoiseOctave2 = 1.0;       // weight of the 0.37 x octave
	double FlowNoiseOctave3 = 0.8;       // weight of the 0.13 x octave
	double FlowEdgeSharpness = 0.6;      // cloud-water ramp at the pattern edges (lower = softer)
	double FlowCloudItcz = 0.5;          // cloud fraction targets of the latitude profile
	double FlowCloudSubtropics = 0.4;
	double FlowCloudMidLatitudes = 0.5;
	double FlowCloudPoles = 0.45;
	double FlowStormCoreRatio = 0.65;    // vortex core radius / storm radius
	double FlowStormInflowDegrees = 22.0;
	double FlowStormCloud = 0.45;        // cloud fraction added inside a storm
	double FlowItczStorminess = 0.6;     // deep clouds in the ITCZ (0..1)
	double FlowStormWindScale = 1.4;     // vortex wind x the storm's max wind
};
