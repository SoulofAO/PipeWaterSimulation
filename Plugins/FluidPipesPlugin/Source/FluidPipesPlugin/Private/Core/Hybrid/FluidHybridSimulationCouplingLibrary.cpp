#include "Core/Hybrid/FluidHybridSimulationCouplingLibrary.h"

#include "Core/Actors/PipeFluidBasePointActor.h"
#include "Core/Actors/PipeFluidPipeActor.h"
#include "Core/Simulation0D/FluidNetwork0DSubsystem.h"
#include "Core/Simulation0D/FluidNetwork0DSimulationStepLibrary.h"
#include "Core/Simulation1D/FluidSegment1DSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FluidPipesDrawDebug.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Other/FluidPipesSimulationSettingsLibrary.h"
#include "Other/LazyFluidPipesDeveloperSettings.h"

static TPair<int32, int32> MakeNormalizedSceneNodeKeyPair(int32 FirstSceneNodeKey, int32 SecondSceneNodeKey)
{
	if (FirstSceneNodeKey <= SecondSceneNodeKey)
	{
		return TPair<int32, int32>(FirstSceneNodeKey, SecondSceneNodeKey);
	}
	return TPair<int32, int32>(SecondSceneNodeKey, FirstSceneNodeKey);
}

static void CollectOrderedUniqueSceneNodeKeys(UWorld* World, TArray<int32>& OutOrderedSceneNodeKeys)
{
	OutOrderedSceneNodeKeys.Reset();
	if (!World)
	{
		return;
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
		OutOrderedSceneNodeKeys.Add(SceneNodeKey);
	}
}

static int32 CountActiveOneDimensionSegmentsAtSceneNodeKey(int32 SceneNodeKey, const FFluidHybridNetworkTopology& Topology, const TArray<FFluidSegmentStateOneD>& SegmentStates)
{
	int32 ActiveIncidentCount = 0;
	for (int32 SegmentIndex = 0; SegmentIndex < SegmentStates.Num(); ++SegmentIndex)
	{
		if (!Topology.SegmentDetailActive.IsValidIndex(SegmentIndex) || !Topology.SegmentDetailActive[SegmentIndex])
		{
			continue;
		}

		const FFluidSegmentStateOneD& SegmentState = SegmentStates[SegmentIndex];
		if (SegmentState.LeftSceneNodeKey == SceneNodeKey || SegmentState.RightSceneNodeKey == SceneNodeKey)
		{
			++ActiveIncidentCount;
		}
	}
	return ActiveIncidentCount;
}

void FFluidHybridSimulationCouplingLibrary::RebuildHybridTopology(
	UWorld* World,
	const TArray<FFluidNetworkNodeStateZeroD>& NetworkNodeStates,
	const TArray<FFluidNetworkEdgeStateZeroD>& NetworkEdgeStates,
	const TArray<FFluidSegmentStateOneD>& SegmentStates,
	const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors,
	FFluidHybridNetworkTopology& OutTopology)
{
	OutTopology = FFluidHybridNetworkTopology();
	OutTopology.SegmentIndexToZeroDEdgeIndex.Init(INDEX_NONE, SegmentStates.Num());
	OutTopology.SegmentDetailActive.Init(false, SegmentStates.Num());
	OutTopology.ZeroDEdgeFlowFixedByOneD.Init(false, NetworkEdgeStates.Num());

	TArray<int32> OrderedSceneNodeKeys;
	CollectOrderedUniqueSceneNodeKeys(World, OrderedSceneNodeKeys);
	OutTopology.ZeroDNodeIndexToSceneNodeKey = OrderedSceneNodeKeys;

	for (int32 NodeIndex = 0; NodeIndex < OrderedSceneNodeKeys.Num(); ++NodeIndex)
	{
		OutTopology.SceneNodeKeyToZeroDNodeIndex.Add(OrderedSceneNodeKeys[NodeIndex], NodeIndex);
	}

	TMap<TPair<int32, int32>, int32> SceneNodeKeyPairToZeroDEdgeIndex;
	for (int32 EdgeIndex = 0; EdgeIndex < NetworkEdgeStates.Num(); ++EdgeIndex)
	{
		const FFluidNetworkEdgeStateZeroD& NetworkEdgeState = NetworkEdgeStates[EdgeIndex];
		if (!OutTopology.ZeroDNodeIndexToSceneNodeKey.IsValidIndex(NetworkEdgeState.FromNodeIndex)
			|| !OutTopology.ZeroDNodeIndexToSceneNodeKey.IsValidIndex(NetworkEdgeState.ToNodeIndex))
		{
			continue;
		}

		const int32 FromSceneNodeKey = OutTopology.ZeroDNodeIndexToSceneNodeKey[NetworkEdgeState.FromNodeIndex];
		const int32 ToSceneNodeKey = OutTopology.ZeroDNodeIndexToSceneNodeKey[NetworkEdgeState.ToNodeIndex];
		SceneNodeKeyPairToZeroDEdgeIndex.Add(MakeNormalizedSceneNodeKeyPair(FromSceneNodeKey, ToSceneNodeKey), EdgeIndex);
	}

	for (int32 SegmentIndex = 0; SegmentIndex < SegmentStates.Num(); ++SegmentIndex)
	{
		const FFluidSegmentStateOneD& SegmentState = SegmentStates[SegmentIndex];
		if (SegmentState.LeftSceneNodeKey == INDEX_NONE || SegmentState.RightSceneNodeKey == INDEX_NONE)
		{
			continue;
		}

		const TPair<int32, int32> SceneNodeKeyPair = MakeNormalizedSceneNodeKeyPair(SegmentState.LeftSceneNodeKey, SegmentState.RightSceneNodeKey);
		if (const int32* EdgeIndex = SceneNodeKeyPairToZeroDEdgeIndex.Find(SceneNodeKeyPair))
		{
			OutTopology.SegmentIndexToZeroDEdgeIndex[SegmentIndex] = *EdgeIndex;
		}
	}

	UpdateHybridDecomposition(World, FFluidPipesSimulationSettingsLibrary::ResolveSimulationSettings(World), SegmentStates, SegmentPipeActors, OutTopology);
}

void FFluidHybridSimulationCouplingLibrary::UpdateHybridDecomposition(
	UWorld* World,
	const ULazyFluidPipesDeveloperSettings& Settings,
	const TArray<FFluidSegmentStateOneD>& SegmentStates,
	const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors,
	FFluidHybridNetworkTopology& InOutTopology)
{
	FVector ReferenceWorldLocation = FVector::ZeroVector;
	bool bReferenceWorldLocationResolved = false;
	if (World)
	{
		if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0))
		{
			ReferenceWorldLocation = PlayerPawn->GetActorLocation();
			bReferenceWorldLocationResolved = true;
		}
	}

	if (!bReferenceWorldLocationResolved)
	{
		FluidPipesTryResolveSimulateInEditorViewportReferenceWorldLocation(ReferenceWorldLocation);
	}

	const float ActivationDistanceSquared = FMath::Square(Settings.HybridOneDActivationDistanceCentimeters);
	for (int32 SegmentIndex = 0; SegmentIndex < SegmentStates.Num(); ++SegmentIndex)
	{
		bool bSegmentDetailActive = false;
		if (SegmentPipeActors.IsValidIndex(SegmentIndex))
		{
			if (const APipeFluidPipeActor* PipeActor = SegmentPipeActors[SegmentIndex].Get())
			{
				const float DistanceSquared = FVector::DistSquared(PipeActor->GetActorLocation(), ReferenceWorldLocation);
				bSegmentDetailActive = DistanceSquared <= ActivationDistanceSquared;
			}
		}
		InOutTopology.SegmentDetailActive[SegmentIndex] = bSegmentDetailActive;
	}

	InOutTopology.ZeroDEdgeFlowFixedByOneD.Init(false, InOutTopology.ZeroDEdgeFlowFixedByOneD.Num());
	for (int32 SegmentIndex = 0; SegmentIndex < SegmentStates.Num(); ++SegmentIndex)
	{
		if (!InOutTopology.SegmentDetailActive.IsValidIndex(SegmentIndex) || !InOutTopology.SegmentDetailActive[SegmentIndex])
		{
			continue;
		}

		const int32 ZeroDEdgeIndex = InOutTopology.SegmentIndexToZeroDEdgeIndex.IsValidIndex(SegmentIndex)
			? InOutTopology.SegmentIndexToZeroDEdgeIndex[SegmentIndex]
			: INDEX_NONE;
		if (InOutTopology.ZeroDEdgeFlowFixedByOneD.IsValidIndex(ZeroDEdgeIndex))
		{
			InOutTopology.ZeroDEdgeFlowFixedByOneD[ZeroDEdgeIndex] = true;
		}
	}
}

void FFluidHybridSimulationCouplingLibrary::ApplyZeroDimensionPressureToOneDimensionBoundaries(
	const FFluidHybridNetworkTopology& Topology,
	const TArray<FFluidNetworkNodeStateZeroD>& NetworkNodeStates,
	TArray<FFluidSegmentStateOneD>& SegmentStates)
{
	for (int32 SegmentIndex = 0; SegmentIndex < SegmentStates.Num(); ++SegmentIndex)
	{
		if (!Topology.SegmentDetailActive.IsValidIndex(SegmentIndex) || !Topology.SegmentDetailActive[SegmentIndex])
		{
			continue;
		}

		FFluidSegmentStateOneD& SegmentState = SegmentStates[SegmentIndex];
		if (SegmentState.CellStates.Num() < 2)
		{
			continue;
		}

		const int32 LeftActiveIncidentCount = CountActiveOneDimensionSegmentsAtSceneNodeKey(SegmentState.LeftSceneNodeKey, Topology, SegmentStates);
		if (SegmentState.LeftBoundaryConditionType != EFluidBoundaryConditionTypeOneD::FixedFlow && LeftActiveIncidentCount < 2)
		{
			if (const int32* ZeroDNodeIndex = Topology.SceneNodeKeyToZeroDNodeIndex.Find(SegmentState.LeftSceneNodeKey))
			{
				if (NetworkNodeStates.IsValidIndex(*ZeroDNodeIndex))
				{
					SegmentState.LeftBoundaryConditionType = EFluidBoundaryConditionTypeOneD::FixedPressure;
					SegmentState.LeftBoundaryPressure = NetworkNodeStates[*ZeroDNodeIndex].Pressure;
				}
			}
		}

		const int32 RightActiveIncidentCount = CountActiveOneDimensionSegmentsAtSceneNodeKey(SegmentState.RightSceneNodeKey, Topology, SegmentStates);
		if (SegmentState.RightBoundaryConditionType != EFluidBoundaryConditionTypeOneD::FixedFlow && RightActiveIncidentCount < 2)
		{
			if (const int32* ZeroDNodeIndex = Topology.SceneNodeKeyToZeroDNodeIndex.Find(SegmentState.RightSceneNodeKey))
			{
				if (NetworkNodeStates.IsValidIndex(*ZeroDNodeIndex))
				{
					SegmentState.RightBoundaryConditionType = EFluidBoundaryConditionTypeOneD::FixedPressure;
					SegmentState.RightBoundaryPressure = NetworkNodeStates[*ZeroDNodeIndex].Pressure;
				}
			}
		}
	}
}

void FFluidHybridSimulationCouplingLibrary::ApplyOneDimensionFlowToZeroDimensionEdges(
	const FFluidHybridNetworkTopology& Topology,
	const TArray<FFluidSegmentStateOneD>& SegmentStates,
	const TArray<FFluidNetworkEdgeStateZeroD>& NetworkEdgeStates,
	TArray<FFluidNetworkEdgeStateZeroD>& MutableNetworkEdgeStates)
{
	for (int32 SegmentIndex = 0; SegmentIndex < SegmentStates.Num(); ++SegmentIndex)
	{
		if (!Topology.SegmentDetailActive.IsValidIndex(SegmentIndex) || !Topology.SegmentDetailActive[SegmentIndex])
		{
			continue;
		}

		const int32 ZeroDEdgeIndex = Topology.SegmentIndexToZeroDEdgeIndex.IsValidIndex(SegmentIndex)
			? Topology.SegmentIndexToZeroDEdgeIndex[SegmentIndex]
			: INDEX_NONE;
		if (!MutableNetworkEdgeStates.IsValidIndex(ZeroDEdgeIndex))
		{
			continue;
		}

		const FFluidSegmentStateOneD& SegmentState = SegmentStates[SegmentIndex];
		if (SegmentState.CellStates.Num() < 2)
		{
			continue;
		}

		const FFluidNetworkEdgeStateZeroD& NetworkEdgeState = NetworkEdgeStates[ZeroDEdgeIndex];
		const int32 FromSceneNodeKey = Topology.ZeroDNodeIndexToSceneNodeKey.IsValidIndex(NetworkEdgeState.FromNodeIndex)
			? Topology.ZeroDNodeIndexToSceneNodeKey[NetworkEdgeState.FromNodeIndex]
			: INDEX_NONE;
		const int32 ToSceneNodeKey = Topology.ZeroDNodeIndexToSceneNodeKey.IsValidIndex(NetworkEdgeState.ToNodeIndex)
			? Topology.ZeroDNodeIndexToSceneNodeKey[NetworkEdgeState.ToNodeIndex]
			: INDEX_NONE;

		const int32 LastCellIndex = SegmentState.CellStates.Num() - 1;
		float SignedEdgeFlow = SegmentState.CellStates[0].FlowRate;
		if (FromSceneNodeKey == SegmentState.RightSceneNodeKey && ToSceneNodeKey == SegmentState.LeftSceneNodeKey)
		{
			SignedEdgeFlow = -SegmentState.CellStates[LastCellIndex].FlowRate;
		}
		else if (FromSceneNodeKey == SegmentState.LeftSceneNodeKey && ToSceneNodeKey == SegmentState.RightSceneNodeKey)
		{
			SignedEdgeFlow = SegmentState.CellStates[0].FlowRate;
		}
		else if (FromSceneNodeKey == SegmentState.RightSceneNodeKey)
		{
			SignedEdgeFlow = SegmentState.CellStates[LastCellIndex].FlowRate;
		}
		else if (ToSceneNodeKey == SegmentState.LeftSceneNodeKey)
		{
			SignedEdgeFlow = -SegmentState.CellStates[0].FlowRate;
		}

		MutableNetworkEdgeStates[ZeroDEdgeIndex].FlowRate = SignedEdgeFlow;
	}
}

void FFluidHybridSimulationCouplingLibrary::RunHybridSimulationStep(
	UWorld* World,
	const ULazyFluidPipesDeveloperSettings& Settings,
	UFluidNetwork0DSubsystem& ZeroDSubsystem,
	UFluidSegment1DSubsystem& OneDSubsystem,
	FFluidHybridNetworkTopology& InOutTopology)
{
	TArray<FFluidNetworkNodeStateZeroD>& NetworkNodeStates = ZeroDSubsystem.GetMutableNetworkNodeStates();
	TArray<FFluidNetworkEdgeStateZeroD>& NetworkEdgeStates = ZeroDSubsystem.GetMutableNetworkEdgeStates();
	TArray<FFluidSegmentStateOneD>& SegmentStates = OneDSubsystem.GetMutableSegmentStates();
	const TArray<FFluidNetworkEdgeStateZeroD>& NetworkEdgeStatesConst = ZeroDSubsystem.GetEdgeStates();

	UpdateHybridDecomposition(World, Settings, SegmentStates, OneDSubsystem.GetSegmentPipeActors(), InOutTopology);

	const int32 CouplingIterationCount = FMath::Max(Settings.HybridInterfaceCouplingIterations, 1);
	for (int32 CouplingIterationIndex = 0; CouplingIterationIndex < CouplingIterationCount; ++CouplingIterationIndex)
	{
		ApplyZeroDimensionPressureToOneDimensionBoundaries(InOutTopology, NetworkNodeStates, SegmentStates);
	}

	const float HybridStepTime = FFluidPipesSimulationSettingsLibrary::ResolveHybridSimulationStepTime(Settings);
	OneDSubsystem.RunCpuGameThreadHybridSimulationStep(HybridStepTime, InOutTopology.SegmentDetailActive);
	ApplyOneDimensionFlowToZeroDimensionEdges(InOutTopology, SegmentStates, NetworkEdgeStatesConst, NetworkEdgeStates);
	ZeroDSubsystem.RunCpuGameThreadHybridSimulationStep(HybridStepTime, InOutTopology.ZeroDEdgeFlowFixedByOneD);
}
