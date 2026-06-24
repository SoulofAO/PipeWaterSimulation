#include "Core/Simulation1D/FluidSegment1DSubsystem.h"

#include "Async/ParallelFor.h"
#include "Core/Actors/PipeFluidBasePointActor.h"
#include "Core/Actors/PipeFluidPipeActor.h"
#include "Core/LevelImport/FluidPipePassiveJunctionMerge.h"
#include "Core/Simulation/FluidSimulationStateLimits.h"
#include "Core/Simulation1D/FluidSegment1DCPUSimulation.h"
#include "Core/Simulation1D/FluidSegment1DGpuSimulation.h"
#include "Core/Simulation1D/FluidSegment1DSimulationLibrary.h"
#include "DrawDebugHelpers.h"
#include "FluidPipesDrawDebug.h"
#include "FluidPipesWorldDebugText.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Core/LazyFluidPipeSubsystem.h"
#include "Other/FluidPipesSimulationSettingsLibrary.h"
#include "Other/LazyFluidPipesDeveloperSettings.h"

void UFluidSegment1DSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ActiveSimulation = MakeUnique<FFluidSegment1DCPUSimulation>();
	ActiveOneDSimulationBackend = EFluidSegmentSimulationOneDBackend::CpuGameThread;
	ResetSimulationState();
}

void UFluidSegment1DSubsystem::Deinitialize()
{
	ResetSimulationState();
	ActiveSimulation.Reset();
	Super::Deinitialize();
}

void UFluidSegment1DSubsystem::Tick(float DeltaTime)
{
	const ULazyFluidPipesDeveloperSettings& Settings = GetSimulationSettings();
	const bool bEnableFluidSegmentSimulationOneD = Settings.EnableFluidSegmentSimulationOneD;
	const bool bFluidHybridSimulationActive = FFluidPipesSimulationSettingsLibrary::IsFluidHybridSimulationActive(Settings);
	if (bEnableFluidSegmentSimulationOneD)
	{
		const bool bPrintSimulationFrameTiming = FluidPipesShouldPrintSimulationFrameTiming();
		ULazyFluidPipeSubsystem* FluidPipeSubsystem = GetWorld() ? GetWorld()->GetSubsystem<ULazyFluidPipeSubsystem>() : nullptr;
		const bool bRecordBenchmarkFrameStats = FluidPipeSubsystem && FluidPipeSubsystem->IsBenchmarkFrameStatsRecordingEnabled();
		const double FrameStartSeconds = (bPrintSimulationFrameTiming || bRecordBenchmarkFrameStats) ? FPlatformTime::Seconds() : 0.0;
		int32 SimulationStepCount = 0;

		const int32 OneDWorldDebugDetailLevel = FluidPipesGetOneDWorldDebugDetailLevel();
		if (OneDWorldDebugDetailLevel > 0 && UsesOffGameThreadOneDSimulationState())
		{
			ReadbackAndDrawOffGameThreadOneDDebug(OneDWorldDebugDetailLevel);
		}

		if (!bFluidHybridSimulationActive)
		{
			AccumulatedTime += DeltaTime;
			while (AccumulatedTime >= Settings.SimulationStepTimeOneD)
			{
				SimulateStep(Settings.SimulationStepTimeOneD);
				AccumulatedTime -= Settings.SimulationStepTimeOneD;
				++SimulationStepCount;
			}
		}

		if (bRecordBenchmarkFrameStats && FluidPipeSubsystem)
		{
			FluidPipeSubsystem->RecordBenchmarkOneDFrameSimulationDurationSeconds(FPlatformTime::Seconds() - FrameStartSeconds);
		}

		if (OneDWorldDebugDetailLevel > 0 && !UsesOffGameThreadOneDSimulationState())
		{
			TArray<int32> SegmentIndicesWithinDebugDrawDistance;
			CollectSegmentIndicesWithinDebugDrawDistance(SegmentIndicesWithinDebugDrawDistance);
			DrawOneDDebugForSegmentIndices(OneDWorldDebugDetailLevel, SegmentIndicesWithinDebugDrawDistance);
		}

		if (FluidPipesShouldEmitScreenDebugMessages())
		{
			UKismetSystemLibrary::PrintString(this, FString::Format(TEXT("1D Tick: Segments={0}"), { FString::FromInt(SegmentStates.Num()) }), true, false, FLinearColor(0.0f, 1.0f, 1.0f), 0.0f);
		}

		if (bPrintSimulationFrameTiming)
		{
			const double ElapsedMilliseconds = (FPlatformTime::Seconds() - FrameStartSeconds) * 1000.0;
			const FString SimulationBackendLabel = BuildOneDSimulationBackendDisplayName(ActiveOneDSimulationBackend);
			const FString TimingMessage = FString::Format(
				TEXT("1D simulation frame: {0} ms | steps={1} | segments={2} | {3}"),
				{
					FString::SanitizeFloat(ElapsedMilliseconds),
					FString::FromInt(SimulationStepCount),
					FString::FromInt(SegmentStates.Num()),
					SimulationBackendLabel
				});
			FluidPipesPrintSimulationFrameTimingMessage(this, TimingMessage, FLinearColor(0.0f, 1.0f, 1.0f));
		}
	}
}

TStatId UFluidSegment1DSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFluidSegment1DSubsystem, STATGROUP_Tickables);
}

void UFluidSegment1DSubsystem::ResetSimulationState()
{
	if (ActiveSimulation)
	{
		ActiveSimulation->Release();
	}
	SegmentStates.Reset();
	SegmentPipeActors.Reset();
	JunctionSceneNodeKeyToIncidentEndpoints.Reset();
	AccumulatedTime = 0.0f;
	ActiveOneDSimulationBackend = EFluidSegmentSimulationOneDBackend::CpuGameThread;
}

const TArray<FFluidSegmentStateOneD>& UFluidSegment1DSubsystem::GetSegmentStates() const
{
	return SegmentStates;
}

TArray<FFluidSegmentStateOneD>& UFluidSegment1DSubsystem::GetMutableSegmentStates()
{
	return SegmentStates;
}

const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& UFluidSegment1DSubsystem::GetSegmentPipeActors() const
{
	return SegmentPipeActors;
}

void UFluidSegment1DSubsystem::RunCpuGameThreadHybridSimulationStep(float SimulationStepTime, const TArray<bool>& SegmentDetailActiveMask)
{
	const ULazyFluidPipesDeveloperSettings& Settings = GetSimulationSettings();
	UpdateOneDimensionBoundaryFlowsFromAttachedPipePointActors();

	UWorld* World = GetWorld();
	const float GravityAcceleration = World ? FMath::Abs(World->GetGravityZ()) : 980.0f;
	const FVector GravityDirectionWorld = FVector::UpVector;

	TArray<float> GravityAccelerationAlongAxis;
	GravityAccelerationAlongAxis.SetNum(SegmentStates.Num());
	for (int32 SegmentIndex = 0; SegmentIndex < SegmentStates.Num(); ++SegmentIndex)
	{
		float GravityAxisComponent = 0.0f;
		if (SegmentPipeActors.IsValidIndex(SegmentIndex))
		{
			if (const APipeFluidPipeActor* PipeActor = SegmentPipeActors[SegmentIndex].Get())
			{
				const FVector PipeAxisDirectionWorld = PipeActor->GetActorForwardVector().GetSafeNormal();
				GravityAxisComponent = FVector::DotProduct(GravityDirectionWorld, PipeAxisDirectionWorld);
			}
		}
		GravityAccelerationAlongAxis[SegmentIndex] = GravityAcceleration * GravityAxisComponent;
	}

	TArray<FFluidSegmentStateOneD> NextSegmentStates;
	NextSegmentStates = SegmentStates;

	const int32 SegmentCount = SegmentStates.Num();
	ParallelFor(SegmentCount, [this, &SegmentDetailActiveMask, &NextSegmentStates, &GravityAccelerationAlongAxis, SimulationStepTime](int32 SegmentIndex)
		{
			if (!SegmentDetailActiveMask.IsValidIndex(SegmentIndex) || !SegmentDetailActiveMask[SegmentIndex])
			{
				return;
			}

			const FFluidSegmentStateOneD& SegmentState = SegmentStates[SegmentIndex];
			if (SegmentState.CellStates.Num() < 2)
			{
				return;
			}

			const float StableStepTime = ComputeStableStepTime(SegmentState);
			const float EffectiveStepTime = FMath::Min(SimulationStepTime, StableStepTime);
			const float GravityAccelerationAlongAxisValue = GravityAccelerationAlongAxis.IsValidIndex(SegmentIndex)
				? GravityAccelerationAlongAxis[SegmentIndex]
				: 0.0f;

			FFluidSegmentStateOneD NextSegmentState = SegmentState;
			SolveSegmentWaterHammerStep(SegmentState, EffectiveStepTime, GravityAccelerationAlongAxisValue, NextSegmentState);
			ApplyBoundaryConditions(SegmentState, NextSegmentState);
			NextSegmentStates[SegmentIndex] = MoveTemp(NextSegmentState);
		});

	JunctionSceneNodeKeyToIncidentEndpoints.Reset();
	for (int32 SegmentIndex = 0; SegmentIndex < SegmentStates.Num(); ++SegmentIndex)
	{
		if (!SegmentDetailActiveMask.IsValidIndex(SegmentIndex) || !SegmentDetailActiveMask[SegmentIndex])
		{
			continue;
		}

		const FFluidSegmentStateOneD& SegmentState = SegmentStates[SegmentIndex];
		if (SegmentState.CellStates.Num() < 2)
		{
			continue;
		}
		if (SegmentState.LeftSceneNodeKey != INDEX_NONE)
		{
			JunctionSceneNodeKeyToIncidentEndpoints.FindOrAdd(SegmentState.LeftSceneNodeKey).Add(FFluidOneDJunctionEndpointIncident{SegmentIndex, true});
		}
		if (SegmentState.RightSceneNodeKey != INDEX_NONE)
		{
			JunctionSceneNodeKeyToIncidentEndpoints.FindOrAdd(SegmentState.RightSceneNodeKey).Add(FFluidOneDJunctionEndpointIncident{SegmentIndex, false});
		}
	}

	ApplyJunctionCouplingToNextSegmentStates(SegmentStates, NextSegmentStates);

	for (int32 SegmentIndex = 0; SegmentIndex < SegmentStates.Num(); ++SegmentIndex)
	{
		if (!SegmentDetailActiveMask.IsValidIndex(SegmentIndex) || !SegmentDetailActiveMask[SegmentIndex])
		{
			continue;
		}

		FFluidSegmentStateOneD& SegmentState = SegmentStates[SegmentIndex];
		FFluidSegmentStateOneD& NextSegmentState = NextSegmentStates[SegmentIndex];
		UpdateDerivedCellValues(NextSegmentState);
		if (!IsSegmentStateFinite(NextSegmentState))
		{
			continue;
		}
		SegmentState = MoveTemp(NextSegmentState);
	}

	FFluidSimulationStateLimits::ClampAllSegmentStatesOneD(SegmentStates, Settings);
	RebuildJunctionSceneNodeKeyTopology(SegmentStates);
}

void UFluidSegment1DSubsystem::ApplyImportedOneDSegments(const TArray<FFluidSegmentStateOneD>& Segments)
{
	TArray<APipeFluidPipeActor*> EmptyPipeActors;
	ApplyImportedOneDSegments(Segments, EmptyPipeActors);
}

void UFluidSegment1DSubsystem::ApplyImportedOneDSegments(const TArray<FFluidSegmentStateOneD>& Segments, const TArray<APipeFluidPipeActor*>& IncomingPipeActors)
{
	SegmentStates = Segments;
	const ULazyFluidPipesDeveloperSettings& Settings = GetSimulationSettings();
	TArray<APipeFluidPipeActor*> MergePipeActors = IncomingPipeActors;
	if (Settings.OneDMergeColinearPassiveJunctionAtImport)
	{
		FFluidPipePassiveJunctionMerge::MergeColinearOneDSegments(SegmentStates, MergePipeActors, GetWorld());
	}
	SegmentPipeActors.Reset();
	if (MergePipeActors.Num() == SegmentStates.Num())
	{
		SegmentPipeActors.Reserve(SegmentStates.Num());
		for (APipeFluidPipeActor* MergePipeActor : MergePipeActors)
		{
			SegmentPipeActors.Add(MergePipeActor);
		}
	}

	for (FFluidSegmentStateOneD& SegmentState : SegmentStates)
	{
		UpdateDerivedCellValues(SegmentState);
	}
	RebuildJunctionSceneNodeKeyTopology(SegmentStates);
	AccumulatedTime = 0.0f;
	EnsureActiveOneDSimulationMatchesSettings(Settings);
}

bool UFluidSegment1DSubsystem::UsesOffGameThreadOneDSimulationState() const
{
	return FluidSegmentSimulationOneDUsesGpuComputeBackend(ActiveOneDSimulationBackend)
		|| ActiveOneDSimulationBackend == EFluidSegmentSimulationOneDBackend::CpuBackgroundThread;
}

FString UFluidSegment1DSubsystem::BuildOneDSimulationBackendDisplayName(EFluidSegmentSimulationOneDBackend Backend)
{
	switch (Backend)
	{
	case EFluidSegmentSimulationOneDBackend::GpuComputeShader:
		return TEXT("GPU");
	case EFluidSegmentSimulationOneDBackend::GpuComputeShaderSynchronous:
		return TEXT("GPU-Sync");
	case EFluidSegmentSimulationOneDBackend::CpuBackgroundThread:
		return TEXT("CPU-Background");
	default:
		return TEXT("CPU-GameThread");
	}
}

const ULazyFluidPipesDeveloperSettings& UFluidSegment1DSubsystem::GetSimulationSettings() const
{
	return FFluidPipesSimulationSettingsLibrary::ResolveSimulationSettings(this);
}

void UFluidSegment1DSubsystem::RebuildActiveSimulationForCurrentSettings()
{
	EnsureActiveOneDSimulationMatchesSettings(GetSimulationSettings());
}

void UFluidSegment1DSubsystem::EnsureActiveOneDSimulationMatchesSettings(const ULazyFluidPipesDeveloperSettings& Settings)
{
	EFluidSegmentSimulationOneDBackend ResolvedBackend = Settings.FluidSegmentSimulationOneDBackend;
	if (FluidSegmentSimulationOneDUsesGpuComputeBackend(ResolvedBackend))
	{
		const TUniquePtr<FFluidSegment1DGpuSimulation> GpuAvailabilityProbe = MakeUnique<FFluidSegment1DGpuSimulation>();
		if (!GpuAvailabilityProbe->IsAvailable())
		{
			ResolvedBackend = EFluidSegmentSimulationOneDBackend::CpuGameThread;
		}
	}

	const bool bNeedsGpuImplementation = FluidSegmentSimulationOneDUsesGpuComputeBackend(ResolvedBackend);
	const bool bActiveUsesGpuImplementation = FluidSegmentSimulationOneDUsesGpuComputeBackend(ActiveOneDSimulationBackend);

	if (bNeedsGpuImplementation == bActiveUsesGpuImplementation && ActiveSimulation)
	{
		ActiveOneDSimulationBackend = ResolvedBackend;
		if (!bNeedsGpuImplementation)
		{
			FFluidSegment1DCPUSimulation* CpuSimulation = static_cast<FFluidSegment1DCPUSimulation*>(ActiveSimulation.Get());
			if (CpuSimulation)
			{
				CpuSimulation->BindSimulationSettings(&Settings);
				const bool bUseBackgroundWorker = ResolvedBackend == EFluidSegmentSimulationOneDBackend::CpuBackgroundThread;
				if (CpuSimulation->UsesBackgroundWorker() != bUseBackgroundWorker)
				{
					CpuSimulation->ConfigureBackgroundWorker(bUseBackgroundWorker);
					if (bUseBackgroundWorker)
					{
						CpuSimulation->PublishSegmentStatesToWorker(SegmentStates);
						CpuSimulation->BakeWorkerStaticStepInputsOnGameThread(GetWorld(), SegmentPipeActors);
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
		ActiveSimulation = MakeUnique<FFluidSegment1DGpuSimulation>();
	}
	else
	{
		ActiveSimulation = MakeUnique<FFluidSegment1DCPUSimulation>();
	}

	ActiveOneDSimulationBackend = ResolvedBackend;

	if (bNeedsGpuImplementation)
	{
		if (FFluidSegment1DGpuSimulation* GpuSimulation = static_cast<FFluidSegment1DGpuSimulation*>(ActiveSimulation.Get()))
		{
			GpuSimulation->RebuildFromSegments(SegmentStates, SegmentPipeActors, GetWorld());
		}
	}
	else
	{
		FFluidSegment1DCPUSimulation* CpuSimulation = static_cast<FFluidSegment1DCPUSimulation*>(ActiveSimulation.Get());
		if (CpuSimulation)
		{
			CpuSimulation->BindSimulationSettings(&Settings);
			const bool bUseBackgroundWorker = ResolvedBackend == EFluidSegmentSimulationOneDBackend::CpuBackgroundThread;
			CpuSimulation->ConfigureBackgroundWorker(bUseBackgroundWorker);
			if (bUseBackgroundWorker)
			{
				CpuSimulation->RebuildFromSegments(SegmentStates, SegmentPipeActors, GetWorld());
			}
			else
			{
				ActiveSimulation->RebuildFromSegments(SegmentStates, SegmentPipeActors);
			}
		}
	}
}

void UFluidSegment1DSubsystem::CollectSegmentIndicesWithinDebugDrawDistance(TArray<int32>& OutSegmentIndices) const
{
	OutSegmentIndices.Reset();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (int32 SegmentIndex = 0; SegmentIndex < SegmentStates.Num(); ++SegmentIndex)
	{
		const APipeFluidPipeActor* PipeActor = SegmentPipeActors.IsValidIndex(SegmentIndex) ? SegmentPipeActors[SegmentIndex].Get() : nullptr;
		if (!PipeActor)
		{
			continue;
		}

		if (FluidPipesIsWorldLocationWithinDebugDrawDistance(World, PipeActor->GetActorLocation()))
		{
			OutSegmentIndices.Add(SegmentIndex);
		}
	}
}

void UFluidSegment1DSubsystem::ReadbackAndDrawOffGameThreadOneDDebug(int32 OneDWorldDebugDetailLevel)
{
	TArray<int32> SegmentIndicesWithinDebugDrawDistance;
	CollectSegmentIndicesWithinDebugDrawDistance(SegmentIndicesWithinDebugDrawDistance);
	if (SegmentIndicesWithinDebugDrawDistance.Num() == 0)
	{
		return;
	}

	if (FluidSegmentSimulationOneDUsesGpuComputeBackend(ActiveOneDSimulationBackend))
	{
		if (FFluidSegment1DGpuSimulation* GpuSimulation = static_cast<FFluidSegment1DGpuSimulation*>(ActiveSimulation.Get()))
		{
			GpuSimulation->ReadbackSegmentIndicesToSegmentStates(SegmentStates, SegmentIndicesWithinDebugDrawDistance);
		}
	}
	else if (FFluidSegment1DCPUSimulation* CpuSimulation = static_cast<FFluidSegment1DCPUSimulation*>(ActiveSimulation.Get()))
	{
		CpuSimulation->ReadbackSegmentIndicesToSegmentStates(SegmentStates, SegmentIndicesWithinDebugDrawDistance);
	}

	DrawOneDDebugForSegmentIndices(OneDWorldDebugDetailLevel, SegmentIndicesWithinDebugDrawDistance);
}

void UFluidSegment1DSubsystem::DrawOneDDebugForSegmentIndices(int32 OneDWorldDebugDetailLevel, const TArray<int32>& SegmentIndicesWithinDebugDrawDistance)
{
	const ULazyFluidPipesDeveloperSettings& Settings = GetSimulationSettings();
	if (Settings.EnableOneDSimulationStateVariableClamping)
	{
		for (const int32 SegmentIndex : SegmentIndicesWithinDebugDrawDistance)
		{
			if (SegmentStates.IsValidIndex(SegmentIndex))
			{
				FFluidSimulationStateLimits::ClampSegmentStateOneD(SegmentStates[SegmentIndex], Settings);
			}
		}
	}
	DrawDebugOneDSegments(OneDWorldDebugDetailLevel);
}

void UFluidSegment1DSubsystem::RebuildJunctionSceneNodeKeyTopology(const TArray<FFluidSegmentStateOneD>& SourceSegmentStates)
{
	JunctionSceneNodeKeyToIncidentEndpoints.Reset();
	for (int32 SegmentIndex = 0; SegmentIndex < SourceSegmentStates.Num(); ++SegmentIndex)
	{
		const FFluidSegmentStateOneD& SegmentState = SourceSegmentStates[SegmentIndex];
		if (SegmentState.CellStates.Num() < 2)
		{
			continue;
		}
		if (SegmentState.LeftSceneNodeKey != INDEX_NONE)
		{
			JunctionSceneNodeKeyToIncidentEndpoints.FindOrAdd(SegmentState.LeftSceneNodeKey).Add(FFluidOneDJunctionEndpointIncident{SegmentIndex, true});
		}
		if (SegmentState.RightSceneNodeKey != INDEX_NONE)
		{
			JunctionSceneNodeKeyToIncidentEndpoints.FindOrAdd(SegmentState.RightSceneNodeKey).Add(FFluidOneDJunctionEndpointIncident{SegmentIndex, false});
		}
	}
}

void UFluidSegment1DSubsystem::ApplyJunctionCouplingToNextSegmentStates(const TArray<FFluidSegmentStateOneD>& CurrentSegmentStates, TArray<FFluidSegmentStateOneD>& NextSegmentStates) const
{
	constexpr float JunctionInteriorPressureRelaxationFraction = 0.25f;
	for (const auto& JunctionEntry : JunctionSceneNodeKeyToIncidentEndpoints)
	{
		const TArray<FFluidOneDJunctionEndpointIncident>& IncidentEndpoints = JunctionEntry.Value;
		if (IncidentEndpoints.Num() < 2)
		{
			continue;
		}

		float WeightedPressureTimesImpedanceSum = 0.0f;
		float AcousticImpedanceSum = 0.0f;
		float SimplePressureSum = 0.0f;
		int32 SimplePressureContributorCount = 0;
		for (const FFluidOneDJunctionEndpointIncident& IncidentEndpoint : IncidentEndpoints)
		{
			if (!NextSegmentStates.IsValidIndex(IncidentEndpoint.SegmentIndex))
			{
				continue;
			}
			const FFluidSegmentStateOneD& NextSegmentState = NextSegmentStates[IncidentEndpoint.SegmentIndex];
			if (NextSegmentState.CellStates.Num() < 2)
			{
				continue;
			}
			const float BranchPressureNearJunction = FFluidSegment1DSimulationLibrary::PressureSampleNearBoundaryForJunctionCoupling(NextSegmentState.CellStates, IncidentEndpoint.bLeftEndpoint);
			SimplePressureSum += BranchPressureNearJunction;
			SimplePressureContributorCount += 1;

			const float CrossSectionArea = GetCrossSectionArea(NextSegmentState);
			const float SafeDensity = FMath::Max(NextSegmentState.Density, KINDA_SMALL_NUMBER);
			const float SafeWaveSpeed = FMath::Max(NextSegmentState.WaveSpeed, 1.0f);
			const float AcousticImpedance = SafeDensity * SafeWaveSpeed / FMath::Max(CrossSectionArea, KINDA_SMALL_NUMBER);
			WeightedPressureTimesImpedanceSum += BranchPressureNearJunction * AcousticImpedance;
			AcousticImpedanceSum += AcousticImpedance;
		}
		if (SimplePressureContributorCount <= 0)
		{
			continue;
		}
		const float JunctionPressure = AcousticImpedanceSum > KINDA_SMALL_NUMBER
			? WeightedPressureTimesImpedanceSum / AcousticImpedanceSum
			: SimplePressureSum / static_cast<float>(SimplePressureContributorCount);

		for (const FFluidOneDJunctionEndpointIncident& IncidentEndpoint : IncidentEndpoints)
		{
			if (!CurrentSegmentStates.IsValidIndex(IncidentEndpoint.SegmentIndex) || !NextSegmentStates.IsValidIndex(IncidentEndpoint.SegmentIndex))
			{
				continue;
			}
			const FFluidSegmentStateOneD& CurrentSegmentState = CurrentSegmentStates[IncidentEndpoint.SegmentIndex];
			const EFluidBoundaryConditionTypeOneD BoundaryConditionType = IncidentEndpoint.bLeftEndpoint ? CurrentSegmentState.LeftBoundaryConditionType : CurrentSegmentState.RightBoundaryConditionType;
			if (BoundaryConditionType != EFluidBoundaryConditionTypeOneD::Reflective)
			{
				continue;
			}

			FFluidSegmentStateOneD& NextSegmentState = NextSegmentStates[IncidentEndpoint.SegmentIndex];
			if (NextSegmentState.CellStates.Num() < 2)
			{
				continue;
			}
			const int32 BoundaryCellIndex = IncidentEndpoint.bLeftEndpoint ? 0 : NextSegmentState.CellStates.Num() - 1;
			const int32 InteriorNeighborCellIndex = IncidentEndpoint.bLeftEndpoint ? 1 : NextSegmentState.CellStates.Num() - 2;
			NextSegmentState.CellStates[BoundaryCellIndex].Pressure = JunctionPressure;
			NextSegmentState.CellStates[BoundaryCellIndex].FlowRate = NextSegmentState.CellStates[InteriorNeighborCellIndex].FlowRate;
			const float InteriorPressureBeforeRelaxation = NextSegmentState.CellStates[InteriorNeighborCellIndex].Pressure;
			NextSegmentState.CellStates[InteriorNeighborCellIndex].Pressure = FMath::Lerp(InteriorPressureBeforeRelaxation, JunctionPressure, JunctionInteriorPressureRelaxationFraction);
		}
	}
}

void UFluidSegment1DSubsystem::UpdateOneDimensionBoundaryFlowsFromAttachedPipePointActors()
{
	UpdateOneDimensionBoundaryFlowsFromAttachedPipePointActors(SegmentStates);
}

void UFluidSegment1DSubsystem::UpdateOneDimensionBoundaryFlowsFromAttachedPipePointActors(TArray<FFluidSegmentStateOneD>& TargetSegmentStates)
{
	for (int32 SegmentIndex = 0; SegmentIndex < TargetSegmentStates.Num(); ++SegmentIndex)
	{
		FFluidSegmentStateOneD& SegmentState = TargetSegmentStates[SegmentIndex];
		if (SegmentState.CellStates.Num() < 2)
		{
			continue;
		}

		if (!SegmentPipeActors.IsValidIndex(SegmentIndex))
		{
			continue;
		}

		APipeFluidPipeActor* PipeActor = SegmentPipeActors[SegmentIndex].Get();
		if (!PipeActor)
		{
			continue;
		}

		if (SegmentState.LeftBoundaryConditionType == EFluidBoundaryConditionTypeOneD::FixedFlow)
		{
			if (APipeFluidBasePointActor* FirstEndpoint = PipeActor->PipeEndpointFirst)
			{
				if (FirstEndpoint->SceneNodeKey == SegmentState.LeftSceneNodeKey)
				{
					const float BoundaryAdjacentCellGaugePressure = SegmentState.CellStates[0].Pressure;
					SegmentState.LeftBoundaryFlow = FirstEndpoint->ComputeRuntimeSignedVolumeFlowRateForOneDimensionPipeBoundary(true, BoundaryAdjacentCellGaugePressure);
				}
			}
		}

		if (SegmentState.RightBoundaryConditionType == EFluidBoundaryConditionTypeOneD::FixedFlow)
		{
			if (APipeFluidBasePointActor* SecondEndpoint = PipeActor->PipeEndpointSecond)
			{
				if (SecondEndpoint->SceneNodeKey == SegmentState.RightSceneNodeKey)
				{
					const int32 LastBoundaryCellIndex = SegmentState.CellStates.Num() - 1;
					const float BoundaryAdjacentCellGaugePressure = SegmentState.CellStates[LastBoundaryCellIndex].Pressure;
					SegmentState.RightBoundaryFlow = SecondEndpoint->ComputeRuntimeSignedVolumeFlowRateForOneDimensionPipeBoundary(false, BoundaryAdjacentCellGaugePressure);
				}
			}
		}
	}
}

void UFluidSegment1DSubsystem::SimulateStep(float SimulationStepTime)
{
	const ULazyFluidPipesDeveloperSettings& Settings = GetSimulationSettings();
	EnsureActiveOneDSimulationMatchesSettings(Settings);

	switch (ActiveOneDSimulationBackend)
	{
	case EFluidSegmentSimulationOneDBackend::GpuComputeShader:
	case EFluidSegmentSimulationOneDBackend::GpuComputeShaderSynchronous:
	{
		float GpuEffectiveStepTime = SimulationStepTime;
		for (const FFluidSegmentStateOneD& SegmentState : SegmentStates)
		{
			GpuEffectiveStepTime = FMath::Min(GpuEffectiveStepTime, ComputeStableStepTime(SegmentState));
		}
		if (FluidPipesShouldEmitScreenDebugMessages() && SimulationStepTime > GpuEffectiveStepTime + KINDA_SMALL_NUMBER)
		{
			UKismetSystemLibrary::PrintString(this, FString::Format(TEXT("1D GPU CFL Limited: Requested={0}, Effective={1}"), { FString::SanitizeFloat(SimulationStepTime), FString::SanitizeFloat(GpuEffectiveStepTime) }), true, true, FLinearColor::Yellow, 0.0f);
		}
		if (FFluidSegment1DGpuSimulation* GpuSimulation = static_cast<FFluidSegment1DGpuSimulation*>(ActiveSimulation.Get()))
		{
			GpuSimulation->SimulateStepGpuOnly(GpuEffectiveStepTime);
			if (FluidSegmentSimulationOneDRequiresGpuStepCompletionWait(ActiveOneDSimulationBackend))
			{
				GpuSimulation->WaitForGpuStepCompletion();
			}
		}
		break;
	}
	case EFluidSegmentSimulationOneDBackend::CpuBackgroundThread:
	{
		if (FFluidSegment1DCPUSimulation* CpuSimulation = static_cast<FFluidSegment1DCPUSimulation*>(ActiveSimulation.Get()))
		{
			CpuSimulation->EnqueueSimulateStepCpuOnly(SimulationStepTime);
		}
		break;
	}
	default:
		if (ActiveSimulation)
		{
			ActiveSimulation->SimulateStep(GetWorld(), SegmentStates, SegmentPipeActors, SimulationStepTime);
		}
		break;
	}
}

void UFluidSegment1DSubsystem::SolveSegmentWaterHammerStep(const FFluidSegmentStateOneD& CurrentSegmentState, float SimulationStepTime, float GravityAccelerationAlongAxis, FFluidSegmentStateOneD& NextSegmentState) const
{
	const float CrossSectionArea = GetCrossSectionArea(CurrentSegmentState);
	const float SafeDensity = FMath::Max(CurrentSegmentState.Density, KINDA_SMALL_NUMBER);
	const float SafeWaveSpeed = FMath::Max(CurrentSegmentState.WaveSpeed, 1.0f);
	const float SafeCellLength = FMath::Max(CurrentSegmentState.CellLength, 0.01f);
	const float FrictionResistance = CurrentSegmentState.FrictionFactor / (2.0f * FMath::Max(CurrentSegmentState.PipeDiameter, 0.001f) * FMath::Max(CrossSectionArea, KINDA_SMALL_NUMBER));
	const float PressureCoefficient = SafeDensity * SafeWaveSpeed * SafeWaveSpeed / FMath::Max(CrossSectionArea, KINDA_SMALL_NUMBER);
	const float GravitySourceTerm = -CrossSectionArea * GravityAccelerationAlongAxis;

	NextSegmentState.CellStates = CurrentSegmentState.CellStates;
	for (int32 CellIndex = 1; CellIndex < CurrentSegmentState.CellStates.Num() - 1; ++CellIndex)
	{
		const float LeftFlow = CurrentSegmentState.CellStates[CellIndex - 1].FlowRate;
		const float CenterFlow = CurrentSegmentState.CellStates[CellIndex].FlowRate;
		const float RightFlow = CurrentSegmentState.CellStates[CellIndex + 1].FlowRate;
		const float LeftPressure = CurrentSegmentState.CellStates[CellIndex - 1].Pressure;
		const float CenterPressure = CurrentSegmentState.CellStates[CellIndex].Pressure;
		const float RightPressure = CurrentSegmentState.CellStates[CellIndex + 1].Pressure;
		const float FlowGradient = (RightFlow - LeftFlow) / (2.0f * SafeCellLength);
		const float PressureGradient = (RightPressure - LeftPressure) / (2.0f * SafeCellLength);
		const float FlowDerivative = -(CrossSectionArea / SafeDensity) * PressureGradient - FrictionResistance * CenterFlow * FMath::Abs(CenterFlow) + GravitySourceTerm;
		const float PressureDerivative = -PressureCoefficient * FlowGradient;

		NextSegmentState.CellStates[CellIndex].FlowRate = CenterFlow + FlowDerivative * SimulationStepTime;
		NextSegmentState.CellStates[CellIndex].Pressure = CenterPressure + PressureDerivative * SimulationStepTime;
	}
}

void UFluidSegment1DSubsystem::ApplyBoundaryConditions(const FFluidSegmentStateOneD& CurrentSegmentState, FFluidSegmentStateOneD& NextSegmentState) const
{
	if (NextSegmentState.CellStates.Num() < 2)
	{
		return;
	}

	const int32 LastCellIndex = NextSegmentState.CellStates.Num() - 1;

	switch (CurrentSegmentState.LeftBoundaryConditionType)
	{
	case EFluidBoundaryConditionTypeOneD::FixedPressure:
		NextSegmentState.CellStates[0].Pressure = CurrentSegmentState.LeftBoundaryPressure;
		NextSegmentState.CellStates[0].FlowRate = NextSegmentState.CellStates[1].FlowRate;
		break;
	case EFluidBoundaryConditionTypeOneD::FixedFlow:
		NextSegmentState.CellStates[0].FlowRate = CurrentSegmentState.LeftBoundaryFlow;
		NextSegmentState.CellStates[0].Pressure = NextSegmentState.CellStates[1].Pressure;
		break;
	default:
		NextSegmentState.CellStates[0].FlowRate = -NextSegmentState.CellStates[1].FlowRate;
		NextSegmentState.CellStates[0].Pressure = NextSegmentState.CellStates[1].Pressure;
		break;
	}

	switch (CurrentSegmentState.RightBoundaryConditionType)
	{
	case EFluidBoundaryConditionTypeOneD::FixedPressure:
		NextSegmentState.CellStates[LastCellIndex].Pressure = CurrentSegmentState.RightBoundaryPressure;
		NextSegmentState.CellStates[LastCellIndex].FlowRate = NextSegmentState.CellStates[LastCellIndex - 1].FlowRate;
		break;
	case EFluidBoundaryConditionTypeOneD::FixedFlow:
		NextSegmentState.CellStates[LastCellIndex].FlowRate = CurrentSegmentState.RightBoundaryFlow;
		NextSegmentState.CellStates[LastCellIndex].Pressure = NextSegmentState.CellStates[LastCellIndex - 1].Pressure;
		break;
	default:
		NextSegmentState.CellStates[LastCellIndex].FlowRate = -NextSegmentState.CellStates[LastCellIndex - 1].FlowRate;
		NextSegmentState.CellStates[LastCellIndex].Pressure = NextSegmentState.CellStates[LastCellIndex - 1].Pressure;
		break;
	}
}

void UFluidSegment1DSubsystem::UpdateDerivedCellValues(FFluidSegmentStateOneD& SegmentState) const
{
	const float CrossSectionArea = GetCrossSectionArea(SegmentState);
	for (FFluidSegmentCellStateOneD& CellState : SegmentState.CellStates)
	{
		CellState.Velocity = CellState.FlowRate / FMath::Max(CrossSectionArea, KINDA_SMALL_NUMBER);
	}
}

float UFluidSegment1DSubsystem::ComputeStableStepTime(const FFluidSegmentStateOneD& SegmentState) const
{
	const ULazyFluidPipesDeveloperSettings& Settings = GetSimulationSettings();
	const float SafeWaveSpeed = FMath::Max(SegmentState.WaveSpeed, 1.0f);
	const float SafeCellLength = FMath::Max(SegmentState.CellLength, 0.01f);
	return Settings.OneDSolverCflFactor * SafeCellLength / SafeWaveSpeed;
}

float UFluidSegment1DSubsystem::GetCrossSectionArea(const FFluidSegmentStateOneD& SegmentState) const
{
	const float SafePipeDiameter = FMath::Max(SegmentState.PipeDiameter, 0.001f);
	return PI * 0.25f * SafePipeDiameter * SafePipeDiameter;
}

bool UFluidSegment1DSubsystem::IsSegmentStateFinite(const FFluidSegmentStateOneD& SegmentState) const
{
	for (const FFluidSegmentCellStateOneD& CellState : SegmentState.CellStates)
	{
		if (!FMath::IsFinite(CellState.Pressure) || !FMath::IsFinite(CellState.FlowRate) || !FMath::IsFinite(CellState.Velocity))
		{
			return false;
		}
	}

	return true;
}

static FString FluidOneDBoundaryStateDisplayString(EFluidBoundaryConditionTypeOneD BoundaryType, float BoundaryPressure, float BoundaryFlow)
{
	switch (BoundaryType)
	{
	case EFluidBoundaryConditionTypeOneD::FixedPressure:
		return FString::Format(TEXT("FixedPressure p={0}"), { FString::SanitizeFloat(BoundaryPressure) });
	case EFluidBoundaryConditionTypeOneD::FixedFlow:
		return FString::Format(TEXT("FixedFlow Q={0}"), { FString::SanitizeFloat(BoundaryFlow) });
	default:
		return FString(TEXT("Reflective"));
	}
}

void UFluidSegment1DSubsystem::DrawDebugOneDSegments(int32 DebugLevel) const
{
	UWorld* World = GetWorld();
	if (!World || DebugLevel <= 0)
	{
		return;
	}
	FlushPersistentDebugLines(World);

	const ULazyFluidPipesDeveloperSettings& WorldDebugSettings = GetSimulationSettings();
	const bool DrawOneDWireGeometry = WorldDebugSettings.WorldDebugIncludeOneDWireGeometry;
	const bool DrawSegmentAndEndpointText = DebugLevel >= 1;
	const bool DrawPerCellText = DebugLevel >= 2;
	const bool DrawEveryCellText = DebugLevel >= 3;
	FluidPipesWorldDebugTextClearWorld(World);

	for (int32 SegmentIndex = 0; SegmentIndex < SegmentStates.Num(); ++SegmentIndex)
	{
		const FFluidSegmentStateOneD& SegmentState = SegmentStates[SegmentIndex];
		if (SegmentState.CellStates.Num() < 1)
		{
			continue;
		}

		const APipeFluidPipeActor* PipeActor = SegmentPipeActors.IsValidIndex(SegmentIndex) ? SegmentPipeActors[SegmentIndex].Get() : nullptr;
		if (!PipeActor)
		{
			continue;
		}

		const FVector AxisDirectionWorld = PipeActor->GetActorForwardVector().GetSafeNormal();
		const FVector CenterWorld = PipeActor->GetActorLocation();
		if (!FluidPipesIsWorldLocationWithinDebugDrawDistance(World, CenterWorld))
		{
			continue;
		}
		const float HalfLength = SegmentState.SegmentLength * 0.5f;
		const FVector AxisStartWorld = CenterWorld - AxisDirectionWorld * HalfLength;
		const FVector AxisEndWorld = CenterWorld + AxisDirectionWorld * HalfLength;
		const int32 CellCount = SegmentState.CellStates.Num();

		float MinPressure = SegmentState.CellStates[0].Pressure;
		float MaxPressure = MinPressure;
		for (const FFluidSegmentCellStateOneD& CellState : SegmentState.CellStates)
		{
			MinPressure = FMath::Min(MinPressure, CellState.Pressure);
			MaxPressure = FMath::Max(MaxPressure, CellState.Pressure);
		}
		const float PressureRange = FMath::Max(MaxPressure - MinPressure, KINDA_SMALL_NUMBER);

		FVector LateralWorld = FVector::CrossProduct(AxisDirectionWorld, FVector::UpVector);
		if (LateralWorld.SizeSquared() < 1.0e-8f)
		{
			LateralWorld = FVector::CrossProduct(AxisDirectionWorld, FVector::ForwardVector);
		}
		LateralWorld = LateralWorld.GetSafeNormal();

		if (DrawOneDWireGeometry)
		{
			DrawDebugLine(World, AxisStartWorld, AxisEndWorld, FColor::White, true, -1.0f, 0, 1.5f);
		}

		const float StableStepTime = ComputeStableStepTime(SegmentState);
		const float CrossSectionArea = GetCrossSectionArea(SegmentState);

		if (DrawSegmentAndEndpointText)
		{
			if (WorldDebugSettings.WorldDebugIncludeOneDSegmentSummary)
			{
				const FString SegmentSummaryLine = FString::Format(
					TEXT("{0} | cells={1} L={2} dx={3} | c={4} D={5} rho={6} f={7} | A={8} dtStable={9} | Pmin={10} Pmax={11}"),
					{
						SegmentState.SegmentName.ToString(),
						FString::FromInt(CellCount),
						FString::SanitizeFloat(SegmentState.SegmentLength),
						FString::SanitizeFloat(SegmentState.CellLength),
						FString::SanitizeFloat(SegmentState.WaveSpeed),
						FString::SanitizeFloat(SegmentState.PipeDiameter),
						FString::SanitizeFloat(SegmentState.Density),
						FString::SanitizeFloat(SegmentState.FrictionFactor),
						FString::SanitizeFloat(CrossSectionArea),
						FString::SanitizeFloat(StableStepTime),
						FString::SanitizeFloat(MinPressure),
						FString::SanitizeFloat(MaxPressure)
					});
				FluidPipesWorldDebugTextQueueString(World, CenterWorld + FVector(0.0f, 0.0f, 48.0f), SegmentSummaryLine, FColor::Yellow, 1.0f);
			}

			if (WorldDebugSettings.WorldDebugIncludeOneDEndpointCaptions)
			{
				const FString LeftEndpointLine = FString::Format(
					TEXT("Start | nodeKey={0} | {1} | cell0 P={2} Q={3}"),
					{
						FString::FromInt(SegmentState.LeftSceneNodeKey),
						FluidOneDBoundaryStateDisplayString(SegmentState.LeftBoundaryConditionType, SegmentState.LeftBoundaryPressure, SegmentState.LeftBoundaryFlow),
						FString::SanitizeFloat(SegmentState.CellStates[0].Pressure),
						FString::SanitizeFloat(SegmentState.CellStates[0].FlowRate)
					});
				FluidPipesWorldDebugTextQueueString(World, AxisStartWorld + FVector(0.0f, 0.0f, 28.0f) + LateralWorld * 18.0f, LeftEndpointLine, FColor::Cyan, 1.0f);

				const FString RightEndpointLine = FString::Format(
					TEXT("End | nodeKey={0} | {1} | cellLast P={2} Q={3}"),
					{
						FString::FromInt(SegmentState.RightSceneNodeKey),
						FluidOneDBoundaryStateDisplayString(SegmentState.RightBoundaryConditionType, SegmentState.RightBoundaryPressure, SegmentState.RightBoundaryFlow),
						FString::SanitizeFloat(SegmentState.CellStates[CellCount - 1].Pressure),
						FString::SanitizeFloat(SegmentState.CellStates[CellCount - 1].FlowRate)
					});
				FluidPipesWorldDebugTextQueueString(World, AxisEndWorld + FVector(0.0f, 0.0f, 28.0f) - LateralWorld * 18.0f, RightEndpointLine, FColor::Cyan, 1.0f);
			}
		}

		const int32 LabelStride = DrawEveryCellText ? 1 : (CellCount <= 10 ? 1 : FMath::Max(1, CellCount / 8));

		for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
		{
			const float SampleParameter = (CellCount <= 1) ? 0.5f : (static_cast<float>(CellIndex) + 0.5f) / static_cast<float>(CellCount);
			const FVector CellPositionWorld = AxisStartWorld + AxisDirectionWorld * (SampleParameter * SegmentState.SegmentLength);
			const float NormalizedPressure = FMath::Clamp((SegmentState.CellStates[CellIndex].Pressure - MinPressure) / PressureRange, 0.0f, 1.0f);
			const FColor PressureColor = FLinearColor::LerpUsingHSV(FLinearColor::Blue, FLinearColor::Red, NormalizedPressure).ToFColor(true);

			if (DrawOneDWireGeometry)
			{
				DrawDebugSphere(World, CellPositionWorld, 8.0f, 8, PressureColor, true, -1.0f, 0, 1.0f);
			}

			const float FlowRate = SegmentState.CellStates[CellIndex].FlowRate;
			if (DrawOneDWireGeometry && FMath::Abs(FlowRate) > KINDA_SMALL_NUMBER)
			{
				const FVector FlowDirectionWorld = AxisDirectionWorld * FMath::Sign(FlowRate);
				const float ArrowHalfLength = FMath::Clamp(FMath::Abs(FlowRate) * 0.001f, 5.0f, 80.0f);
				const FVector ArrowStartWorld = CellPositionWorld - FlowDirectionWorld * ArrowHalfLength * 0.5f;
				const FVector ArrowEndWorld = CellPositionWorld + FlowDirectionWorld * ArrowHalfLength * 0.5f;
				DrawDebugDirectionalArrow(World, ArrowStartWorld, ArrowEndWorld, ArrowHalfLength * 0.35f, FColor(0, 255, 255), true, -1.0f, 0, 2.0f);
			}

			if (WorldDebugSettings.WorldDebugIncludeOneDPerCellCaptions && DrawPerCellText && (CellIndex % LabelStride == 0))
			{
				const FFluidSegmentCellStateOneD& CellState = SegmentState.CellStates[CellIndex];
				const FString CellLabel = FString::Format(
					TEXT("Cell {0} | P={1} Q={2} U={3}"),
					{
						FString::FromInt(CellIndex),
						FString::SanitizeFloat(CellState.Pressure),
						FString::SanitizeFloat(CellState.FlowRate),
						FString::SanitizeFloat(CellState.Velocity)
					});
				const float StaggerScale = 14.0f;
				const FVector StaggerWorld = LateralWorld * StaggerScale * static_cast<float>((CellIndex % 5) - 2);
				FluidPipesWorldDebugTextQueueString(World, CellPositionWorld + FVector(0.0f, 0.0f, 18.0f) + StaggerWorld, CellLabel, FColor::White, 1.0f);
			}
		}
	}
}
