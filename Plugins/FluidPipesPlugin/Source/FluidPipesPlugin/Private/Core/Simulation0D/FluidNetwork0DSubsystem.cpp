#include "Core/Simulation0D/FluidNetwork0DSubsystem.h"

#include "Core/Actors/PipeFluidBasePointActor.h"
#include "Core/LevelImport/FluidPipePassiveJunctionMerge.h"
#include "Core/Simulation/FluidSimulationStateLimits.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "FluidPipesDrawDebug.h"
#include "FluidPipesWorldDebugText.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Other/LazyFluidPipesDeveloperSettings.h"

static void CollectFluidPipeBasePointActorsOrderedSameAsZeroDimensionImport(UWorld* World, TArray<APipeFluidBasePointActor*>& OutOrderedUniqueActors)
{
	OutOrderedUniqueActors.Reset();
	if (!World)
	{
		return;
	}

	TArray<APipeFluidBasePointActor*> AllPointActors;
	for (TActorIterator<APipeFluidBasePointActor> Iterator(World); Iterator; ++Iterator)
	{
		if (*Iterator)
		{
			AllPointActors.Add(*Iterator);
		}
	}

	AllPointActors.Sort([](const APipeFluidBasePointActor& Left, const APipeFluidBasePointActor& Right)
		{
			return Left.SceneNodeKey < Right.SceneNodeKey;
		});

	TSet<int32> SeenSceneNodeKeys;
	for (APipeFluidBasePointActor* PointActor : AllPointActors)
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
		OutOrderedUniqueActors.Add(PointActor);
	}
}

void UFluidNetwork0DSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ResetSimulationState();
}

void UFluidNetwork0DSubsystem::Deinitialize()
{
	ResetSimulationState();
	Super::Deinitialize();
}

void UFluidNetwork0DSubsystem::Tick(float DeltaTime)
{
	const ULazyFluidPipesDeveloperSettings* Settings = GetDefault<ULazyFluidPipesDeveloperSettings>();
	const bool bEnableFluidNetworkSimulationZeroD = Settings->EnableFluidNetworkSimulationZeroD;
	if (bEnableFluidNetworkSimulationZeroD)
	{
		const bool bPrintSimulationFrameTiming = FluidPipesShouldPrintSimulationFrameTiming();
		const double FrameStartSeconds = bPrintSimulationFrameTiming ? FPlatformTime::Seconds() : 0.0;
		int32 SimulationStepCount = 0;

		AccumulatedTime += DeltaTime;
		while (AccumulatedTime >= Settings->SimulationStepTimeZeroD)
		{
			SimulateStep(Settings->SimulationStepTimeZeroD);
			AccumulatedTime -= Settings->SimulationStepTimeZeroD;
			++SimulationStepCount;
		}

		if (FluidPipesShouldDrawZeroDWorldOverlay())
		{
			DrawDebugZeroDWorldOverlay();
		}

		if (FluidPipesShouldEmitScreenDebugMessages())
		{
			UKismetSystemLibrary::PrintString(this, FString::Format(TEXT("0D Tick: Nodes={0}, Edges={1}"), { FString::FromInt(NetworkNodeStates.Num()), FString::FromInt(NetworkEdgeStates.Num()) }), true, false, FLinearColor::Green, 0.0f);
		}

		if (bPrintSimulationFrameTiming)
		{
			const double ElapsedMilliseconds = (FPlatformTime::Seconds() - FrameStartSeconds) * 1000.0;
			const FString TimingMessage = FString::Format(
				TEXT("0D simulation frame: {0} ms | steps={1} | nodes={2} | edges={3}"),
				{
					FString::SanitizeFloat(ElapsedMilliseconds),
					FString::FromInt(SimulationStepCount),
					FString::FromInt(NetworkNodeStates.Num()),
					FString::FromInt(NetworkEdgeStates.Num())
				});
			FluidPipesPrintSimulationFrameTimingMessage(this, TimingMessage, FLinearColor::Green);
		}
	}
}

TStatId UFluidNetwork0DSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFluidNetwork0DSubsystem, STATGROUP_Tickables);
}

void UFluidNetwork0DSubsystem::ResetSimulationState()
{
	NetworkNodeStates.Reset();
	NetworkEdgeStates.Reset();
	AccumulatedTime = 0.0f;
}

const TArray<FFluidNetworkNodeStateZeroD>& UFluidNetwork0DSubsystem::GetNodeStates() const
{
	return NetworkNodeStates;
}

const TArray<FFluidNetworkEdgeStateZeroD>& UFluidNetwork0DSubsystem::GetEdgeStates() const
{
	return NetworkEdgeStates;
}

void UFluidNetwork0DSubsystem::ApplyImportedZeroDNetwork(const TArray<FFluidNetworkNodeStateZeroD>& Nodes, const TArray<FFluidNetworkEdgeStateZeroD>& Edges)
{
	NetworkNodeStates = Nodes;
	NetworkEdgeStates = Edges;
	const ULazyFluidPipesDeveloperSettings* Settings = GetDefault<ULazyFluidPipesDeveloperSettings>();
	if (Settings->ZeroDMergeColinearPassiveJunctionAtImport)
	{
		FFluidPipePassiveJunctionMerge::MergeColinearZeroDEdges(NetworkNodeStates, NetworkEdgeStates, GetWorld());
	}
	AccumulatedTime = 0.0f;
}

void UFluidNetwork0DSubsystem::SimulateStep(float SimulationStepTime)
{
	RefreshNetworkNodeExternalFlowsFromWorldPointActors();
	UpdateEdgeFlows(SimulationStepTime);
	IntegrateNodeVolumes(SimulationStepTime);
	UpdateNodePressures();
	const ULazyFluidPipesDeveloperSettings* Settings = GetDefault<ULazyFluidPipesDeveloperSettings>();
	FFluidSimulationStateLimits::ClampAllNetworkStatesZeroD(NetworkNodeStates, NetworkEdgeStates, *Settings);
}

void UFluidNetwork0DSubsystem::RefreshNetworkNodeExternalFlowsFromWorldPointActors()
{
	UWorld* World = GetWorld();
	if (!World || NetworkNodeStates.Num() == 0)
	{
		return;
	}

	TArray<APipeFluidBasePointActor*> OrderedActors;
	CollectFluidPipeBasePointActorsOrderedSameAsZeroDimensionImport(World, OrderedActors);
	const int32 UpdateCount = FMath::Min(OrderedActors.Num(), NetworkNodeStates.Num());
	for (int32 NodeIndex = 0; NodeIndex < UpdateCount; ++NodeIndex)
	{
		APipeFluidBasePointActor* PointActor = OrderedActors[NodeIndex];
		if (!PointActor)
		{
			continue;
		}

		const float GaugePressure = NetworkNodeStates[NodeIndex].Pressure;
		NetworkNodeStates[NodeIndex].SourceFlow = PointActor->EvaluateRuntimeZeroDimensionExternalVolumeFlowContribution(GaugePressure);
	}
}

void UFluidNetwork0DSubsystem::UpdateEdgeFlows(float SimulationStepTime)
{
	for (FFluidNetworkEdgeStateZeroD& NetworkEdgeState : NetworkEdgeStates)
	{
		if (!NetworkNodeStates.IsValidIndex(NetworkEdgeState.FromNodeIndex) || !NetworkNodeStates.IsValidIndex(NetworkEdgeState.ToNodeIndex))
		{
			NetworkEdgeState.FlowRate = 0.0f;
			continue;
		}

		const float PressureDifference = NetworkNodeStates[NetworkEdgeState.FromNodeIndex].Pressure - NetworkNodeStates[NetworkEdgeState.ToNodeIndex].Pressure;
		if (NetworkEdgeState.Inertance > KINDA_SMALL_NUMBER)
		{
			const float FlowRateDerivative = (PressureDifference - NetworkEdgeState.Resistance * NetworkEdgeState.FlowRate) / NetworkEdgeState.Inertance;
			NetworkEdgeState.FlowRate += FlowRateDerivative * SimulationStepTime;
		}
		else
		{
			NetworkEdgeState.FlowRate = PressureDifference / FMath::Max(NetworkEdgeState.Resistance, KINDA_SMALL_NUMBER);
		}
	}
}

void UFluidNetwork0DSubsystem::IntegrateNodeVolumes(float SimulationStepTime)
{
	TArray<float> NodeNetFlows;
	NodeNetFlows.Init(0.0f, NetworkNodeStates.Num());

	for (const FFluidNetworkEdgeStateZeroD& NetworkEdgeState : NetworkEdgeStates)
	{
		if (NetworkNodeStates.IsValidIndex(NetworkEdgeState.FromNodeIndex))
		{
			NodeNetFlows[NetworkEdgeState.FromNodeIndex] -= NetworkEdgeState.FlowRate;
		}

		if (NetworkNodeStates.IsValidIndex(NetworkEdgeState.ToNodeIndex))
		{
			NodeNetFlows[NetworkEdgeState.ToNodeIndex] += NetworkEdgeState.FlowRate;
		}
	}

	for (int32 NodeIndex = 0; NodeIndex < NetworkNodeStates.Num(); ++NodeIndex)
	{
		NetworkNodeStates[NodeIndex].StoredVolume += (NodeNetFlows[NodeIndex] + NetworkNodeStates[NodeIndex].SourceFlow) * SimulationStepTime;
		NetworkNodeStates[NodeIndex].StoredVolume = FMath::Max(NetworkNodeStates[NodeIndex].StoredVolume, 0.0f);
	}
}

void UFluidNetwork0DSubsystem::UpdateNodePressures()
{
	for (FFluidNetworkNodeStateZeroD& NetworkNodeState : NetworkNodeStates)
	{
		const float SafeCompliance = FMath::Max(NetworkNodeState.Compliance, KINDA_SMALL_NUMBER);
		NetworkNodeState.Pressure = NetworkNodeState.ReferencePressure + (NetworkNodeState.StoredVolume - NetworkNodeState.ReferenceVolume) / SafeCompliance;
	}
}

void UFluidNetwork0DSubsystem::DrawDebugZeroDWorldOverlay() const
{
	UWorld* World = GetWorld();
	if (!World || NetworkNodeStates.Num() == 0)
	{
		return;
	}

	const ULazyFluidPipesDeveloperSettings* WorldDebugSettings = GetDefault<ULazyFluidPipesDeveloperSettings>();
	const bool DrawZeroDWireGeometry = WorldDebugSettings->WorldDebugIncludeZeroDWireGeometry;

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

	TArray<FVector> NodeWorldLocations;
	NodeWorldLocations.Reserve(NetworkNodeStates.Num());
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
		NodeWorldLocations.Add(PointActor->GetActorLocation());
		if (NodeWorldLocations.Num() >= NetworkNodeStates.Num())
		{
			break;
		}
	}

	if (NodeWorldLocations.Num() != NetworkNodeStates.Num())
	{
		return;
	}

	float MinimumPressure = NetworkNodeStates[0].Pressure;
	float MaximumPressure = MinimumPressure;
	for (const FFluidNetworkNodeStateZeroD& NetworkNodeState : NetworkNodeStates)
	{
		MinimumPressure = FMath::Min(MinimumPressure, NetworkNodeState.Pressure);
		MaximumPressure = FMath::Max(MaximumPressure, NetworkNodeState.Pressure);
	}

	const float PressureSpan = FMath::Max(MaximumPressure - MinimumPressure, KINDA_SMALL_NUMBER);

	for (int32 NodeIndex = 0; NodeIndex < NetworkNodeStates.Num(); ++NodeIndex)
	{
		const FVector NodeWorldLocation = NodeWorldLocations[NodeIndex];
		if (!FluidPipesIsWorldLocationWithinDebugDrawDistance(World, NodeWorldLocation))
		{
			continue;
		}
		const FFluidNetworkNodeStateZeroD& NetworkNodeState = NetworkNodeStates[NodeIndex];
		const float NormalizedPressure = FMath::Clamp((NetworkNodeState.Pressure - MinimumPressure) / PressureSpan, 0.0f, 1.0f);
		const FColor PressureDebugColor = FLinearColor::LerpUsingHSV(FLinearColor::Blue, FLinearColor::Red, NormalizedPressure).ToFColor(true);
		if (DrawZeroDWireGeometry)
		{
			DrawDebugSphere(World, NodeWorldLocation, 14.0f, 10, PressureDebugColor, false, 0.0f, 0, 1.5f);
		}

		if (WorldDebugSettings->WorldDebugIncludeZeroDNodeCaptions)
		{
			const FString NodeDebugLabel = FString::Format(
				TEXT("{0} P={1} V={2} SrcQ={3}"),
				{
					NetworkNodeState.NodeName.ToString(),
					FString::SanitizeFloat(NetworkNodeState.Pressure),
					FString::SanitizeFloat(NetworkNodeState.StoredVolume),
					FString::SanitizeFloat(NetworkNodeState.SourceFlow)
				});
			FluidPipesWorldDebugTextQueueString(World, NodeWorldLocation + FVector(0.0f, 0.0f, 28.0f), NodeDebugLabel, FColor::White, 1.1f);
		}
	}

	for (const FFluidNetworkEdgeStateZeroD& NetworkEdgeState : NetworkEdgeStates)
	{
		if (!NetworkNodeStates.IsValidIndex(NetworkEdgeState.FromNodeIndex) || !NetworkNodeStates.IsValidIndex(NetworkEdgeState.ToNodeIndex))
		{
			continue;
		}

		const FVector FromWorldLocation = NodeWorldLocations[NetworkEdgeState.FromNodeIndex];
		const FVector ToWorldLocation = NodeWorldLocations[NetworkEdgeState.ToNodeIndex];
		const FVector EdgeMidWorldLocation = (FromWorldLocation + ToWorldLocation) * 0.5f;
		if (!FluidPipesIsWorldLocationWithinDebugDrawDistance(World, EdgeMidWorldLocation))
		{
			continue;
		}
		const float FlowRateSigned = NetworkEdgeState.FlowRate;
		const FColor FlowDebugColor = FlowRateSigned >= 0.0f ? FColor::Cyan : FColor::Orange;
		const float LineThickness = FMath::Clamp(FMath::Abs(FlowRateSigned) * 0.02f, 1.2f, 10.0f);
		if (DrawZeroDWireGeometry)
		{
			DrawDebugLine(World, FromWorldLocation, ToWorldLocation, FlowDebugColor, false, 0.0f, 0, LineThickness);
		}

		if (DrawZeroDWireGeometry && WorldDebugSettings->WorldDebugIncludeZeroDFlowArrows && FMath::Abs(FlowRateSigned) > KINDA_SMALL_NUMBER)
		{
			const FVector EdgeMidWorld = EdgeMidWorldLocation;
			const FVector EdgeDirectionWorld = (ToWorldLocation - FromWorldLocation).GetSafeNormal();
			const FVector ArrowDirectionWorld = EdgeDirectionWorld * FMath::Sign(FlowRateSigned);
			const float ArrowHalfLength = FMath::Clamp(FMath::Abs(FlowRateSigned) * 0.0015f, 8.0f, 120.0f);
			const FVector ArrowStartWorld = EdgeMidWorld - ArrowDirectionWorld * ArrowHalfLength * 0.5f;
			const FVector ArrowEndWorld = EdgeMidWorld + ArrowDirectionWorld * ArrowHalfLength * 0.5f;
			DrawDebugDirectionalArrow(World, ArrowStartWorld, ArrowEndWorld, ArrowHalfLength * 0.35f, FColor::Yellow, false, 0.0f, 0, 2.0f);
		}
	}
}
