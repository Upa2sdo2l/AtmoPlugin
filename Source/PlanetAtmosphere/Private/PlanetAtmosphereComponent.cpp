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
