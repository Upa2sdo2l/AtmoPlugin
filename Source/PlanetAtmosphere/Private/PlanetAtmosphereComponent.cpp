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
