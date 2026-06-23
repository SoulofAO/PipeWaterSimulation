#include "Core/Simulation0D/FluidNetwork0DSubsystem.h"

#include "Core/Actors/PipeFluidBasePointActor.h"
#include "Core/LevelImport/FluidPipePassiveJunctionMerge.h"
#include "Core/Simulation0D/FluidNetwork0DCPUSimulation.h"
#include "Core/Simulation0D/FluidNetwork0DGpuSimulation.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "FluidPipesDrawDebug.h"
#include "FluidPipesWorldDebugText.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Core/LazyFluidPipeSubsystem.h"
#include "Other/FluidPipesSimulationSettingsLibrary.h"
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
	ActiveSimulation = MakeUnique<FFluidNetwork0DCPUSimulation>();
	ActiveZeroDSimulationBackend = EFluidNetworkSimulationZeroDBackend::CpuGameThread;
	ResetSimulationState();
}

void UFluidNetwork0DSubsystem::Deinitialize()
{
	ResetSimulationState();
	ActiveSimulation.Reset();
	Super::Deinitialize();
}

void UFluidNetwork0DSubsystem::Tick(float DeltaTime)
{
	const ULazyFluidPipesDeveloperSettings& Settings = FFluidPipesSimulationSettingsLibrary::ResolveSimulationSettings(this);
	const bool bEnableFluidNetworkSimulationZeroD = Settings.EnableFluidNetworkSimulationZeroD;
	if (bEnableFluidNetworkSimulationZeroD)
	{
		const bool bPrintSimulationFrameTiming = FluidPipesShouldPrintSimulationFrameTiming();
		ULazyFluidPipeSubsystem* FluidPipeSubsystem = GetWorld() ? GetWorld()->GetSubsystem<ULazyFluidPipeSubsystem>() : nullptr;
		const bool bRecordBenchmarkFrameStats = FluidPipeSubsystem && FluidPipeSubsystem->IsBenchmarkFrameStatsRecordingEnabled();
		const double FrameStartSeconds = (bPrintSimulationFrameTiming || bRecordBenchmarkFrameStats) ? FPlatformTime::Seconds() : 0.0;
		int32 SimulationStepCount = 0;

		if (FluidPipesShouldDrawZeroDWorldOverlay() && UsesOffGameThreadZeroDSimulationState())
		{
			ReadbackAndDrawOffGameThreadZeroDDebug();
		}

		AccumulatedTime += DeltaTime;
		while (AccumulatedTime >= Settings.SimulationStepTimeZeroD)
		{
			SimulateStep(Settings.SimulationStepTimeZeroD);
			AccumulatedTime -= Settings.SimulationStepTimeZeroD;
			++SimulationStepCount;
		}

		if (bRecordBenchmarkFrameStats && FluidPipeSubsystem)
		{
			FluidPipeSubsystem->RecordBenchmarkZeroDFrameSimulationDurationSeconds(FPlatformTime::Seconds() - FrameStartSeconds);
		}

		if (FluidPipesShouldDrawZeroDWorldOverlay() && !UsesOffGameThreadZeroDSimulationState())
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
			const FString SimulationBackendLabel = BuildZeroDSimulationBackendDisplayName(ActiveZeroDSimulationBackend);
			const FString TimingMessage = FString::Format(
				TEXT("0D simulation frame: {0} ms | steps={1} | nodes={2} | edges={3} | {4}"),
				{
					FString::SanitizeFloat(ElapsedMilliseconds),
					FString::FromInt(SimulationStepCount),
					FString::FromInt(NetworkNodeStates.Num()),
					FString::FromInt(NetworkEdgeStates.Num()),
					SimulationBackendLabel
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
	if (ActiveSimulation)
	{
		ActiveSimulation->Release();
	}
	NetworkNodeStates.Reset();
	NetworkEdgeStates.Reset();
	AccumulatedTime = 0.0f;
	ActiveZeroDSimulationBackend = EFluidNetworkSimulationZeroDBackend::CpuGameThread;
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
	const ULazyFluidPipesDeveloperSettings& Settings = FFluidPipesSimulationSettingsLibrary::ResolveSimulationSettings(this);
	if (Settings.ZeroDMergeColinearPassiveJunctionAtImport)
	{
		FFluidPipePassiveJunctionMerge::MergeColinearZeroDEdges(NetworkNodeStates, NetworkEdgeStates, GetWorld());
	}
	AccumulatedTime = 0.0f;
	EnsureActiveZeroDSimulationMatchesSettings(Settings);
	if (ActiveSimulation)
	{
		ActiveSimulation->RebuildFromNetwork(NetworkNodeStates, NetworkEdgeStates, GetWorld());
	}
}

void UFluidNetwork0DSubsystem::RebuildActiveSimulationForCurrentSettings()
{
	EnsureActiveZeroDSimulationMatchesSettings(FFluidPipesSimulationSettingsLibrary::ResolveSimulationSettings(this));
}

bool UFluidNetwork0DSubsystem::UsesOffGameThreadZeroDSimulationState() const
{
	return FluidNetworkSimulationZeroDUsesGpuComputeBackend(ActiveZeroDSimulationBackend)
		|| ActiveZeroDSimulationBackend == EFluidNetworkSimulationZeroDBackend::CpuBackgroundThread;
}

FString UFluidNetwork0DSubsystem::BuildZeroDSimulationBackendDisplayName(EFluidNetworkSimulationZeroDBackend Backend)
{
	switch (Backend)
	{
	case EFluidNetworkSimulationZeroDBackend::GpuComputeShader:
		return TEXT("GPU");
	case EFluidNetworkSimulationZeroDBackend::GpuComputeShaderSynchronous:
		return TEXT("GPU-Sync");
	case EFluidNetworkSimulationZeroDBackend::CpuBackgroundThread:
		return TEXT("CPU-Background");
	default:
		return TEXT("CPU-GameThread");
	}
}

void UFluidNetwork0DSubsystem::EnsureActiveZeroDSimulationMatchesSettings(const ULazyFluidPipesDeveloperSettings& Settings)
{
	EFluidNetworkSimulationZeroDBackend ResolvedBackend = Settings.FluidNetworkSimulationZeroDBackend;
	if (FluidNetworkSimulationZeroDUsesGpuComputeBackend(ResolvedBackend))
	{
		const TUniquePtr<FFluidNetwork0DGpuSimulation> GpuAvailabilityProbe = MakeUnique<FFluidNetwork0DGpuSimulation>();
		if (!GpuAvailabilityProbe->IsAvailable())
		{
			ResolvedBackend = EFluidNetworkSimulationZeroDBackend::CpuGameThread;
		}
	}

	const bool bNeedsGpuImplementation = FluidNetworkSimulationZeroDUsesGpuComputeBackend(ResolvedBackend);
	const bool bActiveUsesGpuImplementation = FluidNetworkSimulationZeroDUsesGpuComputeBackend(ActiveZeroDSimulationBackend);

	if (bNeedsGpuImplementation == bActiveUsesGpuImplementation && ActiveSimulation)
	{
		ActiveZeroDSimulationBackend = ResolvedBackend;
		if (!bNeedsGpuImplementation)
		{
			FFluidNetwork0DCPUSimulation* CpuSimulation = static_cast<FFluidNetwork0DCPUSimulation*>(ActiveSimulation.Get());
			if (CpuSimulation)
			{
				CpuSimulation->BindSimulationSettings(&Settings);
				const bool bUseBackgroundWorker = ResolvedBackend == EFluidNetworkSimulationZeroDBackend::CpuBackgroundThread;
				if (CpuSimulation->UsesBackgroundWorker() != bUseBackgroundWorker)
				{
					CpuSimulation->ConfigureBackgroundWorker(bUseBackgroundWorker);
					if (bUseBackgroundWorker)
					{
						CpuSimulation->PublishNetworkStatesToWorker(NetworkNodeStates, NetworkEdgeStates);
					}
				}
			}
		}
		return;
	}

	if (ActiveSimulation)
	{
		ActiveSimulation->Release();
	}

	if (bNeedsGpuImplementation)
	{
		ActiveSimulation = MakeUnique<FFluidNetwork0DGpuSimulation>();
	}
	else
	{
		ActiveSimulation = MakeUnique<FFluidNetwork0DCPUSimulation>();
	}

	ActiveZeroDSimulationBackend = ResolvedBackend;

	if (ActiveSimulation)
	{
		ActiveSimulation->RebuildFromNetwork(NetworkNodeStates, NetworkEdgeStates, GetWorld());
	}

	if (!bNeedsGpuImplementation)
	{
		FFluidNetwork0DCPUSimulation* CpuSimulation = static_cast<FFluidNetwork0DCPUSimulation*>(ActiveSimulation.Get());
		if (CpuSimulation)
		{
			CpuSimulation->BindSimulationSettings(&Settings);
			const bool bUseBackgroundWorker = ResolvedBackend == EFluidNetworkSimulationZeroDBackend::CpuBackgroundThread;
			CpuSimulation->ConfigureBackgroundWorker(bUseBackgroundWorker);
			if (bUseBackgroundWorker)
			{
				CpuSimulation->PublishNetworkStatesToWorker(NetworkNodeStates, NetworkEdgeStates);
			}
		}
	}
}

void UFluidNetwork0DSubsystem::SimulateStep(float SimulationStepTime)
{
	const ULazyFluidPipesDeveloperSettings& Settings = FFluidPipesSimulationSettingsLibrary::ResolveSimulationSettings(this);
	EnsureActiveZeroDSimulationMatchesSettings(Settings);
	RefreshNetworkNodeExternalFlowsFromWorldPointActors();

	switch (ActiveZeroDSimulationBackend)
	{
	case EFluidNetworkSimulationZeroDBackend::GpuComputeShader:
	case EFluidNetworkSimulationZeroDBackend::GpuComputeShaderSynchronous:
	{
		if (FFluidNetwork0DGpuSimulation* GpuSimulation = static_cast<FFluidNetwork0DGpuSimulation*>(ActiveSimulation.Get()))
		{
			GpuSimulation->UploadNodeSourceFlows(NetworkNodeStates);
			GpuSimulation->SimulateStepGpuOnly(SimulationStepTime);
			if (FluidNetworkSimulationZeroDRequiresGpuStepCompletionWait(ActiveZeroDSimulationBackend))
			{
				GpuSimulation->WaitForGpuStepCompletion();
				GpuSimulation->ReadbackToNetworkStates(NetworkNodeStates, NetworkEdgeStates);
			}
		}
		break;
	}
	case EFluidNetworkSimulationZeroDBackend::CpuBackgroundThread:
	{
		if (FFluidNetwork0DCPUSimulation* CpuSimulation = static_cast<FFluidNetwork0DCPUSimulation*>(ActiveSimulation.Get()))
		{
			CpuSimulation->PublishNodeSourceFlowsToWorker(NetworkNodeStates);
			CpuSimulation->EnqueueSimulateStepCpuOnly(SimulationStepTime);
		}
		break;
	}
	default:
		if (ActiveSimulation)
		{
			ActiveSimulation->SimulateStep(GetWorld(), NetworkNodeStates, NetworkEdgeStates, SimulationStepTime);
		}
		break;
	}
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

void UFluidNetwork0DSubsystem::ReadbackAndDrawOffGameThreadZeroDDebug()
{
	if (FluidNetworkSimulationZeroDUsesGpuComputeBackend(ActiveZeroDSimulationBackend))
	{
		if (FFluidNetwork0DGpuSimulation* GpuSimulation = static_cast<FFluidNetwork0DGpuSimulation*>(ActiveSimulation.Get()))
		{
			GpuSimulation->ReadbackToNetworkStates(NetworkNodeStates, NetworkEdgeStates);
		}
	}
	else if (FFluidNetwork0DCPUSimulation* CpuSimulation = static_cast<FFluidNetwork0DCPUSimulation*>(ActiveSimulation.Get()))
	{
		CpuSimulation->ReadbackToNetworkStates(NetworkNodeStates, NetworkEdgeStates);
	}
	DrawDebugZeroDWorldOverlay();
}

void UFluidNetwork0DSubsystem::DrawDebugZeroDWorldOverlay()
{
	UWorld* World = GetWorld();
	if (!World || NetworkNodeStates.Num() == 0)
	{
		return;
	}

	const ULazyFluidPipesDeveloperSettings& WorldDebugSettings = FFluidPipesSimulationSettingsLibrary::ResolveSimulationSettings(this);
	const bool DrawZeroDWireGeometry = WorldDebugSettings.WorldDebugIncludeZeroDWireGeometry;

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

		if (WorldDebugSettings.WorldDebugIncludeZeroDNodeCaptions)
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

		if (DrawZeroDWireGeometry && WorldDebugSettings.WorldDebugIncludeZeroDFlowArrows && FMath::Abs(FlowRateSigned) > KINDA_SMALL_NUMBER)
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
