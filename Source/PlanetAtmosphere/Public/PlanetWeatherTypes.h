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

	// ---- Step 38: flowmap weather (r.PlanetAtmosphere.Weather.FlowWarp 2). Lengths for an Earth-size planet, scaled with
	// PlanetRadius / 6371 km by the shader (FlowmapWeather.ush). See UPlanetAtmosphereComponent for the meaning.
	double FlowTradeWind = 8.0;
	double FlowJetWind = 22.0;
	double FlowPolarWind = 6.0;
	double FlowMeridionalWind = 4.0;
	double FlowWarpScaleKm = 2039.0;
	double FlowWarpDisplacementKm = 153.0;
	double FlowFineWarpScaleKm = 637.0;
	double FlowFineWarpDisplacementKm = 64.0;
	double FlowNoiseScaleKm = 3058.0;
	double FlowEdgeSharpness = 0.5;
	double FlowClumpScaleKm = 765.0;
	double FlowClumpStrength = 1.0;
	double FlowCloudItcz = 0.4;
	double FlowCloudSubtropics = 0.25;
	double FlowCloudMidLatitudes = 0.5;
	double FlowCloudPoles = 0.35;
	double FlowStormShieldRatio = 3.5;
	double FlowStormRotationDegrees = 51.6;
	double FlowStormCloud = 0.5;
	double FlowItczStorminess = 0.6;
	double FlowSynopticScaleKm = 7136.0;
	double FlowSynopticStrength = 0.35;
	double FlowStreakLengthKm = 1274.0;
	double FlowSlitStrength = 0.6;
	// Step 39: the flowmap model's climate bands do not depend on the rotation (0 = from the rotation, as model C). The edge of
	// the tropical cells in degrees; the mid-latitude / polar edge is halfway to the pole (30 -> 60).
	double FlowTropicsEdgeDegrees = 30.0;
	// Internal (not a setting): set by the renderer to FlowTropicsEdgeDegrees for the flowmap model; > 0 replaces the
	// rotation-derived cell edges in ComputeCellEdges (storm tracks, bands, ITCZ storms follow it).
	double CellEdgeOverrideDegrees = 0.0;
};
