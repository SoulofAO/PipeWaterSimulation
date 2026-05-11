#include "Core/Actors/PipeFluidConsumerActor.h"

#include "Components/DynamicMeshComponent.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "UDynamicMesh.h"

APipeFluidConsumerActor::APipeFluidConsumerActor()
{
}

FFluidNetworkNodeStateZeroD APipeFluidConsumerActor::ImportFluidNetworkNodeStateZeroD() const
{
	FFluidNetworkNodeStateZeroD FluidImportedNetworkNodeStateZeroD;
	FluidImportedNetworkNodeStateZeroD.NodeName = FName(*FString::Format(TEXT("{0}"), { FString::FromInt(SceneNodeKey) }));
	FluidImportedNetworkNodeStateZeroD.SourceFlow = -FMath::Abs(ConsumerVolumeFlowRateDemand);
	return FluidImportedNetworkNodeStateZeroD;
}

FFluidSegmentStateOneD APipeFluidConsumerActor::ImportFluidSegmentStateOneDEndpoint(FFluidSegmentStateOneD Segment, bool bLeftEndpoint) const
{
	const float DemandMagnitude = FMath::Abs(ConsumerVolumeFlowRateDemand);
	if (DemandMagnitude <= KINDA_SMALL_NUMBER)
	{
		return Segment;
	}

	const float SignedBoundaryFlow = bLeftEndpoint ? -DemandMagnitude : DemandMagnitude;

	if (bLeftEndpoint)
	{
		Segment.LeftBoundaryConditionType = EFluidBoundaryConditionTypeOneD::FixedFlow;
		Segment.LeftBoundaryFlow = SignedBoundaryFlow;
	}
	else
	{
		Segment.RightBoundaryConditionType = EFluidBoundaryConditionTypeOneD::FixedFlow;
		Segment.RightBoundaryFlow = SignedBoundaryFlow;
	}
	return Segment;
}

void APipeFluidConsumerActor::RebuildFluidDynamicMesh()
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

	FGeometryScriptPrimitiveOptions PrimitiveOptions;
	const float ConsumerCubeExtentWorldUnits = 22.0f;
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(TargetMesh, PrimitiveOptions, FTransform::Identity, ConsumerCubeExtentWorldUnits, ConsumerCubeExtentWorldUnits, ConsumerCubeExtentWorldUnits, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Center, nullptr);
}
