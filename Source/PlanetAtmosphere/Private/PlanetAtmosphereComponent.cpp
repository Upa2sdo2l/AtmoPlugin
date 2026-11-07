// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlanetAtmosphereComponent.h"
#include "PlanetAtmosphereSceneProxy.h"
#include "AtmosphereWorldSubsystem.h"
#include "Engine/World.h"

UPlanetAtmosphereComponent::UPlanetAtmosphereComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	SetCastShadow(false);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void UPlanetAtmosphereComponent::OnRegister()
{
	Super::OnRegister();

	// Called in Editor, PIE and Game alike (unlike AActor::BeginPlay).
	if (UWorld* World = GetWorld())
	{
		if (UAtmosphereWorldSubsystem* Subsystem = World->GetSubsystem<UAtmosphereWorldSubsystem>())
		{
			Subsystem->RegisterAtmosphere(this);
		}
	}
}

void UPlanetAtmosphereComponent::OnUnregister()
{
	// The subsystem may already be deinitialized during world teardown -> GetSubsystem returns null, nothing to do.
	if (UWorld* World = GetWorld())
	{
		if (UAtmosphereWorldSubsystem* Subsystem = World->GetSubsystem<UAtmosphereWorldSubsystem>())
		{
			Subsystem->UnregisterAtmosphere(this);
		}
	}

	Super::OnUnregister();
}

FPrimitiveSceneProxy* UPlanetAtmosphereComponent::CreateSceneProxy()
{
	return new FPlanetAtmosphereSceneProxy(this);
}

FPlanetAtmosphereRadii UPlanetAtmosphereComponent::GetValidatedRadiiMeters() const
{
	// Same rules as PostEditChangeProperty, but non-mutating, so they also hold
	// for values set at runtime (Blueprint / C++) where the editor clamp never runs.
	constexpr double MinShellThickness = 1000.0; // meters

	FPlanetAtmosphereRadii R;
	R.Planet           = FMath::Max(PlanetRadius, 1.0);
	R.AtmosphereBottom = FMath::Max(AtmosphereBottomRadius, R.Planet);
	R.AtmosphereTop    = FMath::Max(AtmosphereTopRadius, R.AtmosphereBottom + MinShellThickness);
	R.CloudBottom      = FMath::Clamp(CloudBottomRadius, R.AtmosphereBottom, R.AtmosphereTop);
	R.CloudTop         = FMath::Clamp(CloudTopRadius, R.CloudBottom, R.AtmosphereTop);
	return R;
}

FPlanetAtmosphereScattering UPlanetAtmosphereComponent::GetValidatedScatteringUU() const
{
	// Coefficient = Color x Scale (per km) -> per cm (1 km = 100 000 cm). Heights: m -> cm.
	const float MetersToUU = static_cast<float>(PlanetAtmosphere::MetersToUnrealUnits);

	// (Parameter names chosen not to hide any UPrimitiveComponent member: C4458 is an error in UE builds.)
	auto Coefficient = [](const FLinearColor& InColor, float InScalePerKm)
	{
		const float ScalePerCm = FMath::Max(InScalePerKm, 0.0f) * 1.0e-5f;
		return FVector3f(FMath::Max(InColor.R, 0.0f), FMath::Max(InColor.G, 0.0f), FMath::Max(InColor.B, 0.0f)) * ScalePerCm;
	};

	FPlanetAtmosphereScattering S;
	S.RayleighScattering = Coefficient(RayleighScatteringColor, RayleighScatteringScale);
	S.RayleighScaleHeight = FMath::Max(RayleighScaleHeight, 1.0f) * MetersToUU;
	S.MieScattering = Coefficient(MieScatteringColor, MieScatteringScale);
	S.MieAbsorption = Coefficient(MieAbsorptionColor, MieAbsorptionScale);
	S.MieScaleHeight = FMath::Max(MieScaleHeight, 1.0f) * MetersToUU;
	S.MieAnisotropy = FMath::Clamp(MieAnisotropy, 0.0f, 0.999f);
	S.OzoneAbsorption = Coefficient(OzoneAbsorptionColor, OzoneAbsorptionScale);
	S.OzoneLayerAltitude = FMath::Max(OzoneLayerAltitude, 0.0f) * MetersToUU;
	S.OzoneLayerWidth = FMath::Max(OzoneLayerWidth, 1.0f) * MetersToUU;
	S.SurfaceAlbedo = FVector3f(
		FMath::Clamp(SurfaceAlbedo.R, 0.0f, 1.0f),
		FMath::Clamp(SurfaceAlbedo.G, 0.0f, 1.0f),
		FMath::Clamp(SurfaceAlbedo.B, 0.0f, 1.0f));
	return S;
}

FPlanetWeatherParameters UPlanetAtmosphereComponent::GetValidatedWeatherParameters() const
{
	// Same ranges as the UPROPERTY clamps; model units (hours, days, kelvin, km).
	FPlanetWeatherParameters W;
	W.Seed = WeatherSeed;
	W.RotationPeriodHours = FMath::Clamp(static_cast<double>(RotationPeriodHours), 1.0, 2400.0);
	W.bRetrograde = bRetrogradeRotation;
	W.AxialTiltDegrees = FMath::Clamp(static_cast<double>(AxialTilt), 0.0, 90.0);
	W.YearLengthDays = FMath::Max(static_cast<double>(YearLengthDays), 1.0);
	W.SeasonPhase = FMath::Clamp(static_cast<double>(SeasonPhase), 0.0, 1.0);
	W.MeanTemperatureK = FMath::Clamp(static_cast<double>(MeanTemperature), -250.0, 500.0) + 273.15;
	W.EquatorPoleDifferenceK = FMath::Clamp(static_cast<double>(EquatorPoleTemperatureDifference), 0.0, 200.0);
	W.Humidity = FMath::Clamp(static_cast<double>(MeanHumidity), 0.0, 1.0);
	W.CycloneLifetimeDays = FMath::Clamp(static_cast<double>(CycloneLifetimeDays), 0.5, 30.0);
	W.CyclonesPerHemisphere = FMath::Clamp(static_cast<double>(CyclonesPerHemisphere), 0.0, 12.0);
	W.CycloneRadiusKm = FMath::Clamp(CycloneRadius, 50000.0, 5000000.0) / 1000.0;
	W.TropicalCyclonesPerHemisphere = FMath::Clamp(static_cast<double>(TropicalCyclonesPerHemisphere), 0.0, 4.0);
	W.WindScale = FMath::Clamp(static_cast<double>(WindScale), 0.0, 5.0);
	// Step 38: flowmap weather (km for the lengths, radians for the storm rotation).
	W.FlowTradeWind = FMath::Clamp(static_cast<double>(FlowTradeWind), 0.0, 60.0);
	W.FlowJetWind = FMath::Clamp(static_cast<double>(FlowJetWind), 0.0, 80.0);
	W.FlowPolarWind = FMath::Clamp(static_cast<double>(FlowPolarWind), 0.0, 40.0);
	W.FlowMeridionalWind = FMath::Clamp(static_cast<double>(FlowMeridionalWind), 0.0, 20.0);
	W.FlowWarpScaleKm = FMath::Clamp(FlowWarpScale, 100000.0, 20000000.0) / 1000.0;
	W.FlowWarpDisplacementKm = FMath::Clamp(FlowWarpDisplacement, 0.0, 5000000.0) / 1000.0;
	W.FlowFineWarpScaleKm = FMath::Clamp(FlowFineWarpScale, 50000.0, 10000000.0) / 1000.0;
	W.FlowFineWarpDisplacementKm = FMath::Clamp(FlowFineWarpDisplacement, 0.0, 2000000.0) / 1000.0;
	W.FlowNoiseScaleKm = FMath::Clamp(FlowNoiseScale, 200000.0, 20000000.0) / 1000.0;
	W.FlowEdgeSharpness = FMath::Clamp(static_cast<double>(FlowEdgeSharpness), 0.1, 5.0);
	W.FlowClumpScaleKm = FMath::Clamp(FlowClumpScale, 50000.0, 10000000.0) / 1000.0;
	W.FlowClumpStrength = FMath::Clamp(static_cast<double>(FlowClumpStrength), 0.0, 3.0);
	W.FlowCloudItcz = FMath::Clamp(static_cast<double>(FlowCloudItcz), 0.0, 1.0);
	W.FlowCloudSubtropics = FMath::Clamp(static_cast<double>(FlowCloudSubtropics), 0.0, 1.0);
	W.FlowCloudMidLatitudes = FMath::Clamp(static_cast<double>(FlowCloudMidLatitudes), 0.0, 1.0);
	W.FlowCloudPoles = FMath::Clamp(static_cast<double>(FlowCloudPoles), 0.0, 1.0);
	W.FlowStormShieldRatio = FMath::Clamp(static_cast<double>(FlowStormShieldRatio), 0.5, 8.0);
	W.FlowStormRotationDegrees = FMath::Clamp(static_cast<double>(FlowStormRotation), 0.0, 360.0);
	W.FlowStormCloud = FMath::Clamp(static_cast<double>(FlowStormCloud), 0.0, 1.0);
	W.FlowItczStorminess = FMath::Clamp(static_cast<double>(FlowItczStorminess), 0.0, 1.0);
	W.FlowSynopticScaleKm = FMath::Clamp(FlowSynopticScale, 500000.0, 40000000.0) / 1000.0;
	W.FlowSynopticStrength = FMath::Clamp(static_cast<double>(FlowSynopticStrength), 0.0, 1.0);
	W.FlowStreakLengthKm = FMath::Clamp(FlowStreakLength, 0.0, 5000000.0) / 1000.0;
	W.FlowSlitStrength = FMath::Clamp(static_cast<double>(FlowSlitStrength), 0.0, 2.0);
	return W;
}

FBoxSphereBounds UPlanetAtmosphereComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// Bounds = atmosphere top sphere, in Unreal Units (cm), centered on the component.
	// Scale is intentionally ignored: radii are absolute (see class comment), and the proxy
	// uses exactly these bounds (FPrimitiveSceneProxy::GetBounds) for plugin-side frustum culling.
	const double MaxRadiusUU = GetValidatedRadiiUU().AtmosphereTop;

	return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector(MaxRadiusUU), MaxRadiusUU);
}

#if WITH_EDITOR
void UPlanetAtmosphereComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Write the validated values back so the Details panel shows what is actually rendered.
	const FPlanetAtmosphereRadii R = GetValidatedRadiiMeters();
	PlanetRadius           = R.Planet;
	AtmosphereBottomRadius = R.AtmosphereBottom;
	AtmosphereTopRadius    = R.AtmosphereTop;
	CloudBottomRadius      = R.CloudBottom;
	CloudTopRadius         = R.CloudTop;

	// Radii changed -> bounds must be recomputed, and the proxy recreated with new values.
	UpdateBounds();
	MarkRenderStateDirty();
}
#endif
