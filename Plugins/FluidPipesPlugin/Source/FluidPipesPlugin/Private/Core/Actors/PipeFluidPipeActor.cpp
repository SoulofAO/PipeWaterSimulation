#include "Core/Actors/PipeFluidPipeActor.h"

#include "Components/DynamicMeshComponent.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "UDynamicMesh.h"

APipeFluidPipeActor::APipeFluidPipeActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorTickEnabled(false);
}

void APipeFluidPipeActor::BeginPlay()
{
	Super::BeginPlay();
	RefreshObservedEndpointLocations();
	UpdateEndpointFollowingTickEnabled();
}

void APipeFluidPipeActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!PipeEndpointFirst || !PipeEndpointSecond)
	{
		return;
	}

	const FVector FirstWorld = PipeEndpointFirst->GetActorLocation();
	const FVector SecondWorld = PipeEndpointSecond->GetActorLocation();

	const float EndpointMovementToleranceCentimeters = 0.25f;
	if (FirstWorld.Equals(LastObservedEndpointFirstWorld, EndpointMovementToleranceCentimeters)
		&& SecondWorld.Equals(LastObservedEndpointSecondWorld, EndpointMovementToleranceCentimeters))
	{
		return;
	}

	LastObservedEndpointFirstWorld = FirstWorld;
	LastObservedEndpointSecondWorld = SecondWorld;
	UpdateFluidPipeAttachmentToEndpoints();
	RebuildFluidDynamicMesh();
}

void APipeFluidPipeActor::UpdateFluidPipeAttachmentToEndpoints()
{
	if (PipeEndpointFirst && PipeEndpointSecond)
	{
		const FVector FirstWorld = PipeEndpointFirst->GetActorLocation();
		const FVector SecondWorld = PipeEndpointSecond->GetActorLocation();
		const FVector DeltaWorld = SecondWorld - FirstWorld;
		CachedSegmentDistanceWorld = DeltaWorld.Length();
		if (CachedSegmentDistanceWorld > KINDA_SMALL_NUMBER)
		{
			const FVector MiddleWorld = (FirstWorld + SecondWorld) * 0.5f;
			const FRotator SegmentOrientationWorld = DeltaWorld.Rotation();
			SetActorLocationAndRotation(MiddleWorld, SegmentOrientationWorld);
		}
		SegmentPhysicsLength = CachedSegmentDistanceWorld;
	}
	else
	{
		CachedSegmentDistanceWorld = SegmentPhysicsLength;
	}
}

void APipeFluidPipeActor::RefreshObservedEndpointLocations()
{
	if (PipeEndpointFirst && PipeEndpointSecond)
	{
		LastObservedEndpointFirstWorld = PipeEndpointFirst->GetActorLocation();
		LastObservedEndpointSecondWorld = PipeEndpointSecond->GetActorLocation();
	}
}

void APipeFluidPipeActor::UpdateEndpointFollowingTickEnabled()
{
	const UWorld* World = GetWorld();
	const bool FollowEndpointsInSimulatingWorld = World && !World->IsPreviewWorld() && (World->IsGameWorld() || World->IsEditorWorld());
	const bool FollowEndpointsWhenBothEndpointsAssigned = FollowEndpointsInSimulatingWorld && PipeEndpointFirst && PipeEndpointSecond;
	SetActorTickEnabled(FollowEndpointsWhenBothEndpointsAssigned);
}

void APipeFluidPipeActor::OnConstruction(const FTransform& Transform)
{
	UpdateFluidPipeAttachmentToEndpoints();
	RefreshObservedEndpointLocations();
	UpdateEndpointFollowingTickEnabled();
	Super::OnConstruction(Transform);
}

#if WITH_EDITOR
void APipeFluidPipeActor::EditorRefreshFluidPipeAttachmentToAttachedEndpoints()
{
	UpdateFluidPipeAttachmentToEndpoints();
	RefreshObservedEndpointLocations();
	RebuildFluidDynamicMesh();
	UpdateEndpointFollowingTickEnabled();
}
#endif

void APipeFluidPipeActor::RebuildFluidDynamicMesh()
{
	ClearFluidDynamicMeshGeometry();
	if (!FluidDynamicMeshComponent)
	{
		return;
	}

	UDynamicMesh* TargetMesh = FluidDynamicMeshComponent->GetDynamicMesh();
	if (!TargetMesh)
	{
		return;
	}

	const float RadiusWorldUnits = FMath::Max(PipeDiameter * 50.0f, 4.0f);
	const float LengthWorldUnits = FMath::Max(CachedSegmentDistanceWorld, 10.0f);
	const FTransform PrimitiveOrientationWorld(FQuat::FindBetweenNormals(FVector::UnitZ(), FVector::UnitX()), FVector::ZeroVector);
	FGeometryScriptPrimitiveOptions PrimitiveOptions;
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(TargetMesh, PrimitiveOptions, PrimitiveOrientationWorld, RadiusWorldUnits, LengthWorldUnits, 14, 0, true, EGeometryScriptPrimitiveOriginMode::Center, nullptr);
}
