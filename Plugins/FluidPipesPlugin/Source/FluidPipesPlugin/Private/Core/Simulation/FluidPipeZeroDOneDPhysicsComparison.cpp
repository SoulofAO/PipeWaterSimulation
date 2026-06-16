#include "Core/Simulation/FluidPipeZeroDOneDPhysicsComparison.h"

#include "Core/Actors/PipeFluidBasePointActor.h"
#include "Core/Simulation0D/FluidNetwork0DSubsystem.h"
#include "Core/Simulation1D/FluidSegment1DSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/KismetSystemLibrary.h"

static TMap<int32, int32> BuildSceneNodeKeyToZeroDimensionNodeIndexMap(const TArray<FFluidNetworkNodeStateZeroD>& NodeStates, UWorld* World)
{
	TMap<int32, int32> SceneNodeKeyToNodeIndex;
	if (!World)
	{
		return SceneNodeKeyToNodeIndex;
	}

	TArray<APipeFluidBasePointActor*> PointActors;
	for (TActorIterator<APipeFluidBasePointActor> Iterator(World); Iterator; ++Iterator)
	{
		if (*Iterator)
		{
			PointActors.Add(*Iterator);
		}
	}

	PointActors.Sort([](const APipeFluidBasePointActor& Left, const APipeFluidBasePointActor& Right)
		{
			return Left.SceneNodeKey < Right.SceneNodeKey;
		});

	TSet<int32> SeenSceneNodeKeys;
	for (APipeFluidBasePointActor* PointActor : PointActors)
	{
		if (!PointActor)
		{
			continue;
		}

		const int32 SceneNodeKey = PointActor->SceneNodeKey;
		if (SeenSceneNodeKeys.Contains(SceneNodeKey))
		{
			continue;
		}

		SeenSceneNodeKeys.Add(SceneNodeKey);
		const int32 NodeIndex = SceneNodeKeyToNodeIndex.Num();
		if (NodeIndex < NodeStates.Num())
		{
			SceneNodeKeyToNodeIndex.Add(SceneNodeKey, NodeIndex);
		}
	}

	return SceneNodeKeyToNodeIndex;
}

static float ComputeSegmentAveragePressure(const FFluidSegmentStateOneD& SegmentState)
{
	if (SegmentState.CellStates.Num() == 0)
	{
		return 0.0f;
	}

	float PressureSum = 0.0f;
	for (const FFluidSegmentCellStateOneD& CellState : SegmentState.CellStates)
	{
		PressureSum += CellState.Pressure;
	}
	return PressureSum / static_cast<float>(SegmentState.CellStates.Num());
}

static float ComputeSegmentAverageVolumeFlowRate(const FFluidSegmentStateOneD& SegmentState)
{
	if (SegmentState.CellStates.Num() == 0)
	{
		return 0.0f;
	}

	float VolumeFlowRateSum = 0.0f;
	for (const FFluidSegmentCellStateOneD& CellState : SegmentState.CellStates)
	{
		VolumeFlowRateSum += CellState.FlowRate;
	}
	return VolumeFlowRateSum / static_cast<float>(SegmentState.CellStates.Num());
}

static float ComputePressureOrderOfMagnitudeDifference(float ZeroDimensionPressure, float OneDimensionPressure)
{
	const float ZeroDimensionPressureMagnitude = FMath::Max(FMath::Abs(ZeroDimensionPressure), 1.0f);
	const float OneDimensionPressureMagnitude = FMath::Max(FMath::Abs(OneDimensionPressure), 1.0f);
	const float PressureRatio = ZeroDimensionPressureMagnitude / OneDimensionPressureMagnitude;
	const float OrderOfMagnitudeDifference = FMath::Abs(FMath::LogX(10.0f, PressureRatio));
	return OrderOfMagnitudeDifference;
}

FFluidPipeZeroDOneDPhysicsComparisonSummary FFluidPipeZeroDOneDPhysicsComparison::CompareCurrentWorldState(UWorld* World)
{
	FFluidPipeZeroDOneDPhysicsComparisonSummary Summary;
	if (!World)
	{
		return Summary;
	}

	UFluidNetwork0DSubsystem* ZeroDimensionSubsystem = World->GetSubsystem<UFluidNetwork0DSubsystem>();
	UFluidSegment1DSubsystem* OneDimensionSubsystem = World->GetSubsystem<UFluidSegment1DSubsystem>();
	if (!ZeroDimensionSubsystem || !OneDimensionSubsystem)
	{
		return Summary;
	}

	const TArray<FFluidNetworkNodeStateZeroD>& NodeStates = ZeroDimensionSubsystem->GetNodeStates();
	const TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates = ZeroDimensionSubsystem->GetEdgeStates();
	const TArray<FFluidSegmentStateOneD>& SegmentStates = OneDimensionSubsystem->GetSegmentStates();
	if (NodeStates.Num() == 0 || SegmentStates.Num() == 0)
	{
		return Summary;
	}

	const TMap<int32, int32> SceneNodeKeyToNodeIndex = BuildSceneNodeKeyToZeroDimensionNodeIndexMap(NodeStates, World);
	TMap<int32, float> SceneNodeKeyToOneDimensionPressureSum;
	TMap<int32, int32> SceneNodeKeyToOneDimensionPressureSampleCount;

	for (const FFluidSegmentStateOneD& SegmentState : SegmentStates)
	{
		const float SegmentAveragePressure = ComputeSegmentAveragePressure(SegmentState);
		if (SegmentState.LeftSceneNodeKey != INDEX_NONE)
		{
			SceneNodeKeyToOneDimensionPressureSum.FindOrAdd(SegmentState.LeftSceneNodeKey) += SegmentAveragePressure;
			SceneNodeKeyToOneDimensionPressureSampleCount.FindOrAdd(SegmentState.LeftSceneNodeKey) += 1;
		}
		if (SegmentState.RightSceneNodeKey != INDEX_NONE)
		{
			SceneNodeKeyToOneDimensionPressureSum.FindOrAdd(SegmentState.RightSceneNodeKey) += SegmentAveragePressure;
			SceneNodeKeyToOneDimensionPressureSampleCount.FindOrAdd(SegmentState.RightSceneNodeKey) += 1;
		}
	}

	float AbsolutePressureDifferenceSum = 0.0f;
	for (const TPair<int32, int32>& SceneNodeKeyToNodeIndexEntry : SceneNodeKeyToNodeIndex)
	{
		const int32* SampleCount = SceneNodeKeyToOneDimensionPressureSampleCount.Find(SceneNodeKeyToNodeIndexEntry.Key);
		const float* PressureSum = SceneNodeKeyToOneDimensionPressureSum.Find(SceneNodeKeyToNodeIndexEntry.Key);
		if (!SampleCount || !PressureSum || *SampleCount <= 0)
		{
			continue;
		}

		const float ZeroDimensionPressure = NodeStates[SceneNodeKeyToNodeIndexEntry.Value].Pressure;
		const float OneDimensionPressure = *PressureSum / static_cast<float>(*SampleCount);
		const float AbsolutePressureDifference = FMath::Abs(ZeroDimensionPressure - OneDimensionPressure);
		AbsolutePressureDifferenceSum += AbsolutePressureDifference;
		Summary.MaximumPressureOrderOfMagnitudeDifference = FMath::Max(
			Summary.MaximumPressureOrderOfMagnitudeDifference,
			ComputePressureOrderOfMagnitudeDifference(ZeroDimensionPressure, OneDimensionPressure));
		++Summary.ComparedNodeCount;
	}

	if (Summary.ComparedNodeCount > 0)
	{
		Summary.MeanAbsolutePressureDifference = AbsolutePressureDifferenceSum / static_cast<float>(Summary.ComparedNodeCount);
	}

	float AbsoluteVolumeFlowRateDifferenceSum = 0.0f;
	const int32 ComparedFlowCount = FMath::Min(EdgeStates.Num(), SegmentStates.Num());
	for (int32 FlowIndex = 0; FlowIndex < ComparedFlowCount; ++FlowIndex)
	{
		const float ZeroDimensionVolumeFlowRate = EdgeStates[FlowIndex].FlowRate;
		const float OneDimensionVolumeFlowRate = ComputeSegmentAverageVolumeFlowRate(SegmentStates[FlowIndex]);
		AbsoluteVolumeFlowRateDifferenceSum += FMath::Abs(ZeroDimensionVolumeFlowRate - OneDimensionVolumeFlowRate);
	}
	Summary.ComparedFlowCount = ComparedFlowCount;
	if (Summary.ComparedFlowCount > 0)
	{
		Summary.MeanAbsoluteVolumeFlowRateDifference = AbsoluteVolumeFlowRateDifferenceSum / static_cast<float>(Summary.ComparedFlowCount);
	}

	Summary.bComparisonSucceeded = Summary.ComparedNodeCount > 0;
	return Summary;
}

void FFluidPipeZeroDOneDPhysicsComparison::LogComparisonReport(UWorld* World)
{
	const FFluidPipeZeroDOneDPhysicsComparisonSummary Summary = CompareCurrentWorldState(World);
	if (!Summary.bComparisonSucceeded)
	{
		UKismetSystemLibrary::PrintString(World, TEXT("0D/1D physics comparison skipped: no comparable node states."), true, false, FLinearColor(1.0f, 0.35f, 0.0f), 3.0f);
		return;
	}

	const FString ComparisonMessage = FString::Format(
		TEXT("0D/1D physics comparison | nodes={0} mean|dP|={1} max|log10(P0D/P1D)|={2} | flows={3} mean|dQ|={4}"),
		{
			FString::FromInt(Summary.ComparedNodeCount),
			FString::SanitizeFloat(Summary.MeanAbsolutePressureDifference),
			FString::SanitizeFloat(Summary.MaximumPressureOrderOfMagnitudeDifference),
			FString::FromInt(Summary.ComparedFlowCount),
			FString::SanitizeFloat(Summary.MeanAbsoluteVolumeFlowRateDifference)
		});
	UKismetSystemLibrary::PrintString(World, ComparisonMessage, true, false, FLinearColor(0.2f, 0.9f, 1.0f), 5.0f);
}
