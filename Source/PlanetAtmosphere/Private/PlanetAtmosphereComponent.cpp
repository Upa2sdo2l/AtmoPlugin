// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlanetAtmosphereComponent.h"
#include "PlanetAtmosphereSceneProxy.h"

UPlanetAtmosphereComponent::UPlanetAtmosphereComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	
	// Enable rendering
	SetCastShadow(false);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

FPrimitiveSceneProxy* UPlanetAtmosphereComponent::CreateSceneProxy()
{
	return new FPlanetAtmosphereSceneProxy(this);
}

FBoxSphereBounds UPlanetAtmosphereComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// Bounds are based on atmosphere radius for frustum culling
	const double MaxRadius = FMath::Max(AtmosphereTopRadius, CloudTopRadius);
	
	FBoxSphereBounds ComponentBounds;
	ComponentBounds.Origin = FVector::ZeroVector; // Component-relative
	ComponentBounds.SphereRadius = MaxRadius;
	ComponentBounds.BoxExtent = FVector(MaxRadius);
	
	return ComponentBounds.TransformBy(LocalToWorld);
}

#if WITH_EDITOR
void UPlanetAtmosphereComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	// Clamp values to ensure consistency
	AtmosphereBottomRadius = FMath::Max(AtmosphereBottomRadius, PlanetRadius);
	AtmosphereTopRadius = FMath::Max(AtmosphereTopRadius, AtmosphereBottomRadius + 1000.0);
	CloudBottomRadius = FMath::Clamp(CloudBottomRadius, AtmosphereBottomRadius, AtmosphereTopRadius);
	CloudTopRadius = FMath::Clamp(CloudTopRadius, CloudBottomRadius, AtmosphereTopRadius);
	
	// Notify render thread to rebuild proxy
	MarkRenderStateDirty();
}
#endif
