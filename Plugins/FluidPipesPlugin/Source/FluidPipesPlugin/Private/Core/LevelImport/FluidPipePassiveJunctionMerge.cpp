#include "Core/LevelImport/FluidPipePassiveJunctionMerge.h"

#include "Core/Actors/PipeFluidBasePointActor.h"
#include "Core/Actors/PipeFluidConsumerActor.h"
#include "Core/Actors/PipeFluidPipeActor.h"
#include "Core/Actors/PipeFluidPointActor.h"
#include "Core/Actors/PipeFluidPressureConsumerActor.h"
#include "Core/Actors/PipeFluidSourceActor.h"
#include "EngineUtils.h"

struct FFluidOneDJunctionIncidentBinding
{
	int32 SegmentIndex = INDEX_NONE;
	bool bLeftEndpoint = false;
};

static bool FluidPipeIsPassiveJunctionSceneNode(UWorld* World, int32 SceneNodeKey)
{
	if (!World || SceneNodeKey == INDEX_NONE)
	{
		return false;
	}
	for (TActorIterator<APipeFluidBasePointActor> Iterator(World); Iterator; ++Iterator)
	{
		APipeFluidBasePointActor* PointActor = *Iterator;
		if (!PointActor || PointActor->SceneNodeKey != SceneNodeKey)
		{
			continue;
		}
		if (Cast<APipeFluidSourceActor>(PointActor) || Cast<APipeFluidConsumerActor>(PointActor) || Cast<APipeFluidPressureConsumerActor>(PointActor))
		{
			return false;
		}
		return Cast<APipeFluidPointActor>(PointActor) != nullptr;
	}
	return false;
}

static APipeFluidBasePointActor* FluidPipeFindPointActorBySceneNodeKey(UWorld* World, int32 SceneNodeKey)
{
	if (!World || SceneNodeKey == INDEX_NONE)
	{
		return nullptr;
	}

	for (TActorIterator<APipeFluidBasePointActor> Iterator(World); Iterator; ++Iterator)
	{
		APipeFluidBasePointActor* PointActor = *Iterator;
		if (PointActor && PointActor->SceneNodeKey == SceneNodeKey)
		{
			return PointActor;
		}
	}

	return nullptr;
}

static bool FluidPipeAreZeroDEdgesColinearAtMiddleNode(UWorld* World, int32 FirstOuterSceneNodeKey, int32 MiddleSceneNodeKey, int32 SecondOuterSceneNodeKey)
{
	APipeFluidBasePointActor* FirstOuterPointActor = FluidPipeFindPointActorBySceneNodeKey(World, FirstOuterSceneNodeKey);
	APipeFluidBasePointActor* MiddlePointActor = FluidPipeFindPointActorBySceneNodeKey(World, MiddleSceneNodeKey);
	APipeFluidBasePointActor* SecondOuterPointActor = FluidPipeFindPointActorBySceneNodeKey(World, SecondOuterSceneNodeKey);
	if (!FirstOuterPointActor || !MiddlePointActor || !SecondOuterPointActor)
	{
		return false;
	}

	const FVector DirectionToFirstOuter = (FirstOuterPointActor->GetActorLocation() - MiddlePointActor->GetActorLocation()).GetSafeNormal();
	const FVector DirectionToSecondOuter = (SecondOuterPointActor->GetActorLocation() - MiddlePointActor->GetActorLocation()).GetSafeNormal();
	const float DirectionDot = FVector::DotProduct(DirectionToFirstOuter, DirectionToSecondOuter);
	return DirectionDot <= -0.99f;
}

static bool FluidPipeOneDSegmentPhysicsMatches(const FFluidSegmentStateOneD& FirstSegment, const FFluidSegmentStateOneD& SecondSegment)
{
	const float DiameterTolerance = 0.001f;
	const float ScalarTolerance = 0.01f;
	return FMath::IsNearlyEqual(FirstSegment.PipeDiameter, SecondSegment.PipeDiameter, DiameterTolerance)
		&& FMath::IsNearlyEqual(FirstSegment.WaveSpeed, SecondSegment.WaveSpeed, ScalarTolerance)
		&& FMath::IsNearlyEqual(FirstSegment.Density, SecondSegment.Density, ScalarTolerance)
		&& FMath::IsNearlyEqual(FirstSegment.FrictionFactor, SecondSegment.FrictionFactor, ScalarTolerance);
}

static bool FluidPipeOneDPipesColinear(APipeFluidPipeActor* FirstPipeActor, APipeFluidPipeActor* SecondPipeActor)
{
	if (!FirstPipeActor || !SecondPipeActor)
	{
		return false;
	}
	const FVector FirstForward = FirstPipeActor->GetActorForwardVector().GetSafeNormal();
	const FVector SecondForward = SecondPipeActor->GetActorForwardVector().GetSafeNormal();
	const float AlignmentDot = FVector::DotProduct(FirstForward, SecondForward);
	return AlignmentDot >= 0.99f;
}

static bool FluidPipeCanMergeOneDIncidentPair(
	const FFluidSegmentStateOneD& FirstSegment,
	bool bFirstLeftEndpoint,
	const FFluidSegmentStateOneD& SecondSegment,
	bool bSecondLeftEndpoint,
	APipeFluidPipeActor* FirstPipeActor,
	APipeFluidPipeActor* SecondPipeActor,
	UWorld* World)
{
	if (FirstSegment.CellStates.Num() < 2 || SecondSegment.CellStates.Num() < 2)
	{
		return false;
	}

	const int32 FirstJunctionSceneNodeKey = bFirstLeftEndpoint ? FirstSegment.LeftSceneNodeKey : FirstSegment.RightSceneNodeKey;
	const int32 SecondJunctionSceneNodeKey = bSecondLeftEndpoint ? SecondSegment.LeftSceneNodeKey : SecondSegment.RightSceneNodeKey;
	if (FirstJunctionSceneNodeKey == INDEX_NONE || FirstJunctionSceneNodeKey != SecondJunctionSceneNodeKey)
	{
		return false;
	}

	if (!FluidPipeIsPassiveJunctionSceneNode(World, FirstJunctionSceneNodeKey))
	{
		return false;
	}

	const EFluidBoundaryConditionTypeOneD FirstBoundary = bFirstLeftEndpoint ? FirstSegment.LeftBoundaryConditionType : FirstSegment.RightBoundaryConditionType;
	const EFluidBoundaryConditionTypeOneD SecondBoundary = bSecondLeftEndpoint ? SecondSegment.LeftBoundaryConditionType : SecondSegment.RightBoundaryConditionType;
	if (FirstBoundary != EFluidBoundaryConditionTypeOneD::Reflective || SecondBoundary != EFluidBoundaryConditionTypeOneD::Reflective)
	{
		return false;
	}

	if (!FluidPipeOneDSegmentPhysicsMatches(FirstSegment, SecondSegment))
	{
		return false;
	}

	return FluidPipeOneDPipesColinear(FirstPipeActor, SecondPipeActor);
}

static void FluidPipeMergeOneDSegmentPair(
	const FFluidSegmentStateOneD& FirstSegment,
	bool bFirstLeftEndpoint,
	const FFluidSegmentStateOneD& SecondSegment,
	bool bSecondLeftEndpoint,
	FFluidSegmentStateOneD& OutMergedSegment)
{
	const bool bFirstSegmentIsLeftSide = !bFirstLeftEndpoint && bSecondLeftEndpoint;
	if (bFirstSegmentIsLeftSide)
	{
		OutMergedSegment = FirstSegment;
		OutMergedSegment.RightSceneNodeKey = SecondSegment.RightSceneNodeKey;
		OutMergedSegment.RightBoundaryConditionType = SecondSegment.RightBoundaryConditionType;
		OutMergedSegment.RightBoundaryPressure = SecondSegment.RightBoundaryPressure;
		OutMergedSegment.RightBoundaryFlow = SecondSegment.RightBoundaryFlow;
		if (SecondSegment.CellStates.Num() > 1)
		{
			OutMergedSegment.CellStates.Append(SecondSegment.CellStates.GetData() + 1, SecondSegment.CellStates.Num() - 1);
		}
	}
	else
	{
		OutMergedSegment = SecondSegment;
		OutMergedSegment.RightSceneNodeKey = FirstSegment.RightSceneNodeKey;
		OutMergedSegment.RightBoundaryConditionType = FirstSegment.RightBoundaryConditionType;
		OutMergedSegment.RightBoundaryPressure = FirstSegment.RightBoundaryPressure;
		OutMergedSegment.RightBoundaryFlow = FirstSegment.RightBoundaryFlow;
		if (FirstSegment.CellStates.Num() > 1)
		{
			OutMergedSegment.CellStates.Append(FirstSegment.CellStates.GetData() + 1, FirstSegment.CellStates.Num() - 1);
		}
	}

	OutMergedSegment.SegmentLength = FirstSegment.SegmentLength + SecondSegment.SegmentLength;
	const int32 MergedCellCount = OutMergedSegment.CellStates.Num();
	if (MergedCellCount > 0)
	{
		OutMergedSegment.CellLength = FMath::Max(OutMergedSegment.SegmentLength / static_cast<float>(MergedCellCount), 0.01f);
	}
}

void FFluidPipePassiveJunctionMerge::MergeColinearOneDSegments(TArray<FFluidSegmentStateOneD>& SegmentStates, TArray<APipeFluidPipeActor*>& SegmentPipeActors, UWorld* World)
{
	if (SegmentStates.Num() < 2)
	{
		return;
	}

	const bool bHasMatchingPipeActors = SegmentPipeActors.Num() == SegmentStates.Num();
	bool bMergedAny = true;
	while (bMergedAny)
	{
		bMergedAny = false;
		TMap<int32, TArray<FFluidOneDJunctionIncidentBinding>> JunctionSceneNodeKeyToIncidents;
		for (int32 SegmentIndex = 0; SegmentIndex < SegmentStates.Num(); ++SegmentIndex)
		{
			const FFluidSegmentStateOneD& SegmentState = SegmentStates[SegmentIndex];
			if (SegmentState.CellStates.Num() < 2)
			{
				continue;
			}
			if (SegmentState.LeftSceneNodeKey != INDEX_NONE)
			{
				JunctionSceneNodeKeyToIncidents.FindOrAdd(SegmentState.LeftSceneNodeKey).Add(FFluidOneDJunctionIncidentBinding{ SegmentIndex, true });
			}
			if (SegmentState.RightSceneNodeKey != INDEX_NONE)
			{
				JunctionSceneNodeKeyToIncidents.FindOrAdd(SegmentState.RightSceneNodeKey).Add(FFluidOneDJunctionIncidentBinding{ SegmentIndex, false });
			}
		}

		for (const TPair<int32, TArray<FFluidOneDJunctionIncidentBinding>>& JunctionEntry : JunctionSceneNodeKeyToIncidents)
		{
			const TArray<FFluidOneDJunctionIncidentBinding>& Incidents = JunctionEntry.Value;
			if (Incidents.Num() != 2)
			{
				continue;
			}

			const FFluidOneDJunctionIncidentBinding& FirstIncident = Incidents[0];
			const FFluidOneDJunctionIncidentBinding& SecondIncident = Incidents[1];
			if (FirstIncident.SegmentIndex == SecondIncident.SegmentIndex)
			{
				continue;
			}

			const FFluidSegmentStateOneD& FirstSegment = SegmentStates[FirstIncident.SegmentIndex];
			const FFluidSegmentStateOneD& SecondSegment = SegmentStates[SecondIncident.SegmentIndex];
			APipeFluidPipeActor* FirstPipeActor = bHasMatchingPipeActors ? SegmentPipeActors[FirstIncident.SegmentIndex] : nullptr;
			APipeFluidPipeActor* SecondPipeActor = bHasMatchingPipeActors ? SegmentPipeActors[SecondIncident.SegmentIndex] : nullptr;
			if (!FluidPipeCanMergeOneDIncidentPair(FirstSegment, FirstIncident.bLeftEndpoint, SecondSegment, SecondIncident.bLeftEndpoint, FirstPipeActor, SecondPipeActor, World))
			{
				continue;
			}

			FFluidSegmentStateOneD MergedSegment;
			FluidPipeMergeOneDSegmentPair(FirstSegment, FirstIncident.bLeftEndpoint, SecondSegment, SecondIncident.bLeftEndpoint, MergedSegment);

			const int32 RemoveSegmentIndex = FMath::Max(FirstIncident.SegmentIndex, SecondIncident.SegmentIndex);
			const int32 KeepSegmentIndex = FMath::Min(FirstIncident.SegmentIndex, SecondIncident.SegmentIndex);
			SegmentStates[KeepSegmentIndex] = MoveTemp(MergedSegment);
			if (bHasMatchingPipeActors)
			{
				SegmentPipeActors[KeepSegmentIndex] = SegmentPipeActors[KeepSegmentIndex] ? SegmentPipeActors[KeepSegmentIndex] : SegmentPipeActors[RemoveSegmentIndex];
			}
			SegmentStates.RemoveAt(RemoveSegmentIndex);
			if (bHasMatchingPipeActors)
			{
				SegmentPipeActors.RemoveAt(RemoveSegmentIndex);
			}
			bMergedAny = true;
			break;
		}
	}
}

static TArray<int32> FluidPipeBuildOrderedSceneNodeKeysMatchingZeroDImport(UWorld* World)
{
	TArray<int32> OrderedSceneNodeKeys;
	if (!World)
	{
		return OrderedSceneNodeKeys;
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
		OrderedSceneNodeKeys.Add(SceneNodeKey);
	}
	return OrderedSceneNodeKeys;
}

void FFluidPipePassiveJunctionMerge::MergeColinearZeroDEdges(TArray<FFluidNetworkNodeStateZeroD>& NodeStates, TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, UWorld* World)
{
	if (NodeStates.Num() < 2 || EdgeStates.Num() < 2 || !World)
	{
		return;
	}

	TArray<int32> OrderedSceneNodeKeys = FluidPipeBuildOrderedSceneNodeKeysMatchingZeroDImport(World);
	if (OrderedSceneNodeKeys.Num() != NodeStates.Num())
	{
		return;
	}

	bool bMergedAny = true;
	while (bMergedAny)
	{
		bMergedAny = false;
		for (int32 MiddleNodeIndex = 0; MiddleNodeIndex < NodeStates.Num(); ++MiddleNodeIndex)
		{
			const int32 MiddleSceneNodeKey = OrderedSceneNodeKeys[MiddleNodeIndex];
			if (!FluidPipeIsPassiveJunctionSceneNode(World, MiddleSceneNodeKey))
			{
				continue;
			}

			TArray<int32> IncidentEdgeIndices;
			for (int32 EdgeIndex = 0; EdgeIndex < EdgeStates.Num(); ++EdgeIndex)
			{
				const FFluidNetworkEdgeStateZeroD& EdgeState = EdgeStates[EdgeIndex];
				if (EdgeState.FromNodeIndex == MiddleNodeIndex || EdgeState.ToNodeIndex == MiddleNodeIndex)
				{
					IncidentEdgeIndices.Add(EdgeIndex);
				}
			}

			if (IncidentEdgeIndices.Num() != 2)
			{
				continue;
			}

			const FFluidNetworkEdgeStateZeroD& FirstEdge = EdgeStates[IncidentEdgeIndices[0]];
			const FFluidNetworkEdgeStateZeroD& SecondEdge = EdgeStates[IncidentEdgeIndices[1]];
			const auto ResolveOuterNodeIndex = [MiddleNodeIndex](const FFluidNetworkEdgeStateZeroD& EdgeState) -> int32
			{
				if (EdgeState.FromNodeIndex == MiddleNodeIndex)
				{
					return EdgeState.ToNodeIndex;
				}
				if (EdgeState.ToNodeIndex == MiddleNodeIndex)
				{
					return EdgeState.FromNodeIndex;
				}
				return INDEX_NONE;
			};

			const int32 OuterNodeIndexFirst = ResolveOuterNodeIndex(FirstEdge);
			const int32 OuterNodeIndexSecond = ResolveOuterNodeIndex(SecondEdge);
			if (OuterNodeIndexFirst == INDEX_NONE || OuterNodeIndexSecond == INDEX_NONE)
			{
				continue;
			}
			if (!OrderedSceneNodeKeys.IsValidIndex(OuterNodeIndexFirst) || !OrderedSceneNodeKeys.IsValidIndex(OuterNodeIndexSecond))
			{
				continue;
			}

			const int32 FirstOuterSceneNodeKey = OrderedSceneNodeKeys[OuterNodeIndexFirst];
			const int32 MiddleSceneNodeKeyForMerge = OrderedSceneNodeKeys[MiddleNodeIndex];
			const int32 SecondOuterSceneNodeKey = OrderedSceneNodeKeys[OuterNodeIndexSecond];
			if (!FluidPipeAreZeroDEdgesColinearAtMiddleNode(World, FirstOuterSceneNodeKey, MiddleSceneNodeKeyForMerge, SecondOuterSceneNodeKey))
			{
				continue;
			}

			FFluidNetworkEdgeStateZeroD MergedEdge;
			MergedEdge.FromNodeIndex = OuterNodeIndexFirst;
			MergedEdge.ToNodeIndex = OuterNodeIndexSecond;
			MergedEdge.Resistance = FirstEdge.Resistance + SecondEdge.Resistance;
			MergedEdge.Inertance = FirstEdge.Inertance + SecondEdge.Inertance;
			MergedEdge.FlowRate = 0.5f * (FirstEdge.FlowRate + SecondEdge.FlowRate);

			const int32 RemoveEdgeIndexHigh = FMath::Max(IncidentEdgeIndices[0], IncidentEdgeIndices[1]);
			const int32 RemoveEdgeIndexLow = FMath::Min(IncidentEdgeIndices[0], IncidentEdgeIndices[1]);
			EdgeStates[RemoveEdgeIndexLow] = MergedEdge;
			EdgeStates.RemoveAt(RemoveEdgeIndexHigh);
			NodeStates.RemoveAt(MiddleNodeIndex);
			OrderedSceneNodeKeys.RemoveAt(MiddleNodeIndex);

			for (FFluidNetworkEdgeStateZeroD& EdgeState : EdgeStates)
			{
				if (EdgeState.FromNodeIndex > MiddleNodeIndex)
				{
					--EdgeState.FromNodeIndex;
				}
				if (EdgeState.ToNodeIndex > MiddleNodeIndex)
				{
					--EdgeState.ToNodeIndex;
				}
			}

			bMergedAny = true;
			break;
		}
	}
}
