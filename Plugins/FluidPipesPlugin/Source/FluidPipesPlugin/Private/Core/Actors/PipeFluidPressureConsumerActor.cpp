#include "Core/Actors/PipeFluidPressureConsumerActor.h"

#include "Components/DynamicMeshComponent.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "UDynamicMesh.h"

APipeFluidPressureConsumerActor::APipeFluidPressureConsumerActor()
{
}

float APipeFluidPressureConsumerActor::ComputePressureConsumerVolumeFlowRateMagnitudeForGaugePressure(float GaugePressure) const
{
	const float PressureExcessOverReference = FMath::Max(0.0f, GaugePressure - ConsumerReferenceGaugePressure);
	const float UncappedPressureConsumerVolumeFlowRate = PressureExcessOverReference * ConsumerVolumeFlowRatePerGaugePressureExcess;
	return FMath::Clamp(UncappedPressureConsumerVolumeFlowRate, MinimumPressureConsumerVolumeFlowRate, MaximumPressureConsumerVolumeFlowRate);
}

FFluidNetworkNodeStateZeroD APipeFluidPressureConsumerActor::ImportFluidNetworkNodeStateZeroD() const
{
	FFluidNetworkNodeStateZeroD FluidImportedNetworkNodeStateZeroD;
	FluidImportedNetworkNodeStateZeroD.NodeName = FName(*FString::Format(TEXT("{0}"), { FString::FromInt(SceneNodeKey) }));
	FluidImportedNetworkNodeStateZeroD.SourceFlow = EvaluateRuntimeZeroDimensionExternalVolumeFlowContribution(0.0f);
	return FluidImportedNetworkNodeStateZeroD;
}

FFluidSegmentStateOneD APipeFluidPressureConsumerActor::ImportFluidSegmentStateOneDEndpoint(FFluidSegmentStateOneD Segment, bool bLeftEndpoint) const
{
	const float InitialSignedBoundaryFlow = ComputeRuntimeSignedVolumeFlowRateForOneDimensionPipeBoundary(bLeftEndpoint, 0.0f);
	if (bLeftEndpoint)
	{
		Segment.LeftBoundaryConditionType = EFluidBoundaryConditionTypeOneD::FixedFlow;
		Segment.LeftBoundaryFlow = InitialSignedBoundaryFlow;
	}
	else
	{
		Segment.RightBoundaryConditionType = EFluidBoundaryConditionTypeOneD::FixedFlow;
		Segment.RightBoundaryFlow = InitialSignedBoundaryFlow;
	}
	return Segment;
}

float APipeFluidPressureConsumerActor::EvaluateRuntimeZeroDimensionExternalVolumeFlowContribution(float NodeGaugePressure) const
{
	const float PressureConsumerVolumeFlowRateMagnitude = ComputePressureConsumerVolumeFlowRateMagnitudeForGaugePressure(NodeGaugePressure);
	return -PressureConsumerVolumeFlowRateMagnitude;
}

float APipeFluidPressureConsumerActor::ComputeRuntimeSignedVolumeFlowRateForOneDimensionPipeBoundary(bool bLowAxisPipeAttachedEndpoint, float BoundaryAdjacentCellGaugePressure) const
{
	const float PressureConsumerVolumeFlowRateMagnitude = ComputePressureConsumerVolumeFlowRateMagnitudeForGaugePressure(BoundaryAdjacentCellGaugePressure);
	return bLowAxisPipeAttachedEndpoint ? -PressureConsumerVolumeFlowRateMagnitude : PressureConsumerVolumeFlowRateMagnitude;
}

void APipeFluidPressureConsumerActor::RebuildFluidDynamicMesh()
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
	const float PressureConsumerPrismHalfExtentWorldUnits = 18.0f;
	const float PressureConsumerPrismLengthWorldUnits = 26.0f;
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(TargetMesh, PrimitiveOptions, FTransform::Identity, PressureConsumerPrismHalfExtentWorldUnits, PressureConsumerPrismHalfExtentWorldUnits, PressureConsumerPrismLengthWorldUnits, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Center, nullptr);
}
