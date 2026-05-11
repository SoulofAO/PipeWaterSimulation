#include "Core/LevelImport/FluidPipeLevelImportSubsystem.h"

#include "Core/Actors/PipeFluidBasePointActor.h"
#include "Core/Actors/PipeFluidPipeActor.h"
#include "Core/Simulation0D/FluidNetwork0DSubsystem.h"
#include "Core/Simulation1D/FluidSegment1DSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FluidPipesDrawDebug.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Other/LazyFluidPipesDeveloperSettings.h"

static void ImportFluidActorsIntoZeroDSubsystem(UWorld* World)
{
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

	TArray<FFluidNetworkNodeStateZeroD> Nodes;
	TMap<int32, int32> SceneNodeKeyToNodeIndex;
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
			if (FluidPipesShouldEmitScreenDebugMessages())
			{
				UKismetSystemLibrary::PrintString(World, FString::Format(TEXT("Level pipe import skipped duplicate SceneNodeKey={0}"), { FString::FromInt(SceneNodeKey) }), true, false, FLinearColor(1.0f, 0.35f, 0.0f), 2.0f);
			}
			continue;
		}

		SeenSceneNodeKeys.Add(SceneNodeKey);
		SceneNodeKeyToNodeIndex.Add(SceneNodeKey, Nodes.Num());
		Nodes.Add(PointActor->ImportFluidNetworkNodeStateZeroD());
	}

	TArray<FFluidNetworkEdgeStateZeroD> Edges;
	for (TActorIterator<APipeFluidPipeActor> Iterator(World); Iterator; ++Iterator)
	{
		APipeFluidPipeActor* PipeActor = *Iterator;
		if (!PipeActor || !PipeActor->PipeEndpointFirst || !PipeActor->PipeEndpointSecond)
		{
			if (PipeActor && FluidPipesShouldEmitScreenDebugMessages() && (!PipeActor->PipeEndpointFirst || !PipeActor->PipeEndpointSecond))
			{
				UKismetSystemLibrary::PrintString(World, TEXT("Level pipe import skipped pipe with invalid endpoint references"), true, false, FLinearColor(1.0f, 0.35f, 0.0f), 2.0f);
			}
			continue;
		}

		const int32* FromNodeIndex = SceneNodeKeyToNodeIndex.Find(PipeActor->PipeEndpointFirst->SceneNodeKey);
		const int32* ToNodeIndex = SceneNodeKeyToNodeIndex.Find(PipeActor->PipeEndpointSecond->SceneNodeKey);
		if (!FromNodeIndex || !ToNodeIndex)
		{
			if (FluidPipesShouldEmitScreenDebugMessages())
			{
				UKismetSystemLibrary::PrintString(World, TEXT("Level pipe import skipped pipe with unresolved endpoint SceneNodeKey"), true, false, FLinearColor(1.0f, 0.35f, 0.0f), 2.0f);
			}
			continue;
		}

		FFluidNetworkEdgeStateZeroD EdgeState;
		EdgeState.FromNodeIndex = *FromNodeIndex;
		EdgeState.ToNodeIndex = *ToNodeIndex;
		EdgeState.Resistance = PipeActor->EdgeResistance;
		EdgeState.Inertance = PipeActor->EdgeInertance;
		EdgeState.FlowRate = PipeActor->EdgeInitialFlowRate;
		Edges.Add(EdgeState);
	}

	UFluidNetwork0DSubsystem* ZeroDSubsystem = World->GetSubsystem<UFluidNetwork0DSubsystem>();
	if (ZeroDSubsystem)
	{
		ZeroDSubsystem->ApplyImportedZeroDNetwork(Nodes, Edges);
	}

	if (FluidPipesShouldEmitScreenDebugMessages())
	{
		UKismetSystemLibrary::PrintString(World, FString::Format(TEXT("Level pipe import 0D: Nodes={0}, Edges={1}"), { FString::FromInt(Nodes.Num()), FString::FromInt(Edges.Num()) }), true, false, FLinearColor::Green, 2.0f);
	}
}

static void ImportFluidActorsIntoOneDSubsystem(UWorld* World)
{
	TArray<FFluidSegmentStateOneD> BuiltSegments;
	TArray<APipeFluidPipeActor*> IncomingPipeActors;

	for (TActorIterator<APipeFluidPipeActor> Iterator(World); Iterator; ++Iterator)
	{
		APipeFluidPipeActor* PipeActor = *Iterator;
		if (!PipeActor)
		{
			continue;
		}

		const int32 SafeCellCount = FMath::Max(PipeActor->SimulationCellCount, 2);
		FFluidSegmentStateOneD BuiltSegment;
		BuiltSegment.SegmentName = PipeActor->PipeSegmentName;
		BuiltSegment.SegmentLength = PipeActor->SegmentPhysicsLength;
		BuiltSegment.WaveSpeed = PipeActor->WaveSpeed;
		BuiltSegment.PipeDiameter = PipeActor->PipeDiameter;
		BuiltSegment.FrictionFactor = PipeActor->FrictionFactor;
		BuiltSegment.Density = PipeActor->Density;
		BuiltSegment.LeftBoundaryConditionType = EFluidBoundaryConditionTypeOneD::Reflective;
		BuiltSegment.RightBoundaryConditionType = EFluidBoundaryConditionTypeOneD::Reflective;
		BuiltSegment.LeftSceneNodeKey = PipeActor->PipeEndpointFirst ? PipeActor->PipeEndpointFirst->SceneNodeKey : INDEX_NONE;
		BuiltSegment.RightSceneNodeKey = PipeActor->PipeEndpointSecond ? PipeActor->PipeEndpointSecond->SceneNodeKey : INDEX_NONE;
		if (PipeActor->PipeEndpointFirst)
		{
			BuiltSegment = PipeActor->PipeEndpointFirst->ImportFluidSegmentStateOneDEndpoint(BuiltSegment, true);
		}
		if (PipeActor->PipeEndpointSecond)
		{
			BuiltSegment = PipeActor->PipeEndpointSecond->ImportFluidSegmentStateOneDEndpoint(BuiltSegment, false);
		}
		BuiltSegment.CellStates.Reset();
		BuiltSegment.CellStates.SetNum(SafeCellCount);
		BuiltSegment.CellLength = FMath::Max(BuiltSegment.SegmentLength / static_cast<float>(SafeCellCount), 0.01f);

		for (FFluidSegmentCellStateOneD& CellState : BuiltSegment.CellStates)
		{
			CellState.Pressure = PipeActor->InitialCellPressure;
			CellState.FlowRate = PipeActor->InitialCellFlowRate;
			CellState.Velocity = 0.0f;
			CellState.FillRatio = 0.5f;
		}

		BuiltSegments.Add(BuiltSegment);
		IncomingPipeActors.Add(PipeActor);
	}

	UFluidSegment1DSubsystem* OneDSubsystem = World->GetSubsystem<UFluidSegment1DSubsystem>();
	if (OneDSubsystem)
	{
		OneDSubsystem->ApplyImportedOneDSegments(BuiltSegments, IncomingPipeActors);
	}

	if (FluidPipesShouldEmitScreenDebugMessages())
	{
		UKismetSystemLibrary::PrintString(World, FString::Format(TEXT("Level pipe import 1D: Segments={0}"), { FString::FromInt(BuiltSegments.Num()) }), true, false, FLinearColor(0.0f, 1.0f, 1.0f), 2.0f);
	}
}

void UFluidPipeLevelImportSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const ULazyFluidPipesDeveloperSettings* Settings = GetDefault<ULazyFluidPipesDeveloperSettings>();
	if (Settings->LevelPipeImportTarget == EFluidLevelPipeImportTarget::Disabled)
	{
		bImportFinished = true;
	}
}

void UFluidPipeLevelImportSubsystem::Deinitialize()
{
	bImportFinished = true;
	Super::Deinitialize();
}

void UFluidPipeLevelImportSubsystem::Tick(float DeltaTime)
{
	if (bImportFinished)
	{
		return;
	}

	RunLevelPipeImport();
	bImportFinished = true;
}

TStatId UFluidPipeLevelImportSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFluidPipeLevelImportSubsystem, STATGROUP_Tickables);
}

bool UFluidPipeLevelImportSubsystem::IsTickable() const
{
	return !bImportFinished;
}

void UFluidPipeLevelImportSubsystem::RunLevelPipeImport()
{
	UWorld* World = GetWorld();
	const ULazyFluidPipesDeveloperSettings* Settings = GetDefault<ULazyFluidPipesDeveloperSettings>();
	if (!World || Settings->LevelPipeImportTarget == EFluidLevelPipeImportTarget::Disabled)
	{
		return;
	}

	if (Settings->LevelPipeImportTarget == EFluidLevelPipeImportTarget::ZeroDNetwork)
	{
		ImportFluidActorsIntoZeroDSubsystem(World);
		return;
	}

	if (Settings->LevelPipeImportTarget == EFluidLevelPipeImportTarget::OneDSegments)
	{
		ImportFluidActorsIntoOneDSubsystem(World);
		return;
	}

	if (Settings->LevelPipeImportTarget == EFluidLevelPipeImportTarget::Both)
	{
		ImportFluidActorsIntoZeroDSubsystem(World);
		ImportFluidActorsIntoOneDSubsystem(World);
	}
}
