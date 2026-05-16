#include "Core/Simulation1D/FluidSegment1DCPUSimulation.h"

#include "Async/ParallelFor.h"
#include "Core/Actors/PipeFluidBasePointActor.h"
#include "Core/Actors/PipeFluidPipeActor.h"
#include "Core/Simulation/FluidSimulationStateLimits.h"
#include "Core/Simulation1D/FluidSegment1DSimulationLibrary.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Other/FluidPipesSimulationSettingsLibrary.h"
#include "Other/LazyFluidPipesDeveloperSettings.h"

class FFluidSegment1DCpuWorkerRunnable : public FRunnable
{
public:
	explicit FFluidSegment1DCpuWorkerRunnable(FFluidSegment1DCPUSimulation* InSimulation)
		: Simulation(InSimulation)
	{
	}

	virtual uint32 Run() override
	{
		if (Simulation)
		{
			Simulation->WorkerThreadMainLoop();
		}
		return 0;
	}

	virtual void Stop() override
	{
		if (Simulation)
		{
			Simulation->RequestWorkerStop();
		}
	}

private:
	FFluidSegment1DCPUSimulation* Simulation = nullptr;
};

FFluidSegment1DCPUSimulation::FFluidSegment1DCPUSimulation()
{
	StepRequestedEvent = FPlatformProcess::GetSynchEventFromPool(false);
	StepCompletedEvent = FPlatformProcess::GetSynchEventFromPool(false);
	StepCompletedEvent->Trigger();
}

FFluidSegment1DCPUSimulation::~FFluidSegment1DCPUSimulation()
{
	Release();
}

bool FFluidSegment1DCPUSimulation::IsAvailable() const
{
	return true;
}

void FFluidSegment1DCPUSimulation::Release()
{
	WaitForStepCompletion();
	StopBackgroundWorker();
	JunctionSceneNodeKeyToIncidentEndpoints.Reset();
	{
		FScopeLock WorkerStateLock(&WorkerStateCriticalSection);
		WorkerSegmentStates.Reset();
		SegmentGravityAccelerationAlongAxis.Reset();
		PendingSimulationStepTimes.Reset();
		bWorkerStateResident = false;
	}
	if (StepRequestedEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(StepRequestedEvent);
		StepRequestedEvent = nullptr;
	}
	if (StepCompletedEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(StepCompletedEvent);
		StepCompletedEvent = nullptr;
	}
}

void FFluidSegment1DCPUSimulation::BindSimulationSettings(const ULazyFluidPipesDeveloperSettings* SimulationSettings)
{
	BoundSimulationSettings = SimulationSettings;
}

void FFluidSegment1DCPUSimulation::ConfigureBackgroundWorker(bool bEnableBackgroundWorker)
{
	if (bBackgroundWorkerEnabled == bEnableBackgroundWorker)
	{
		return;
	}

	if (bBackgroundWorkerEnabled)
	{
		StopBackgroundWorker();
	}

	bBackgroundWorkerEnabled = bEnableBackgroundWorker;

	if (bBackgroundWorkerEnabled)
	{
		EnsureBackgroundWorkerRunning();
		StepCompletedEvent->Trigger();
	}
}

bool FFluidSegment1DCPUSimulation::UsesBackgroundWorker() const
{
	return bBackgroundWorkerEnabled;
}

bool FFluidSegment1DCPUSimulation::IsWorkerStateResident() const
{
	return bWorkerStateResident;
}

void FFluidSegment1DCPUSimulation::RebuildFromSegments(const TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors)
{
	RebuildFromSegments(SegmentStates, SegmentPipeActors, nullptr);
}

void FFluidSegment1DCPUSimulation::RebuildFromSegments(const TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors, UWorld* SimulationWorld)
{
	RebuildJunctionSceneNodeKeyTopology(SegmentStates);

	if (bBackgroundWorkerEnabled)
	{
		PublishSegmentStatesToWorker(SegmentStates);
		BakeWorkerStaticStepInputsOnGameThread(SimulationWorld, SegmentPipeActors);
	}
}

void FFluidSegment1DCPUSimulation::PublishSegmentStatesToWorker(const TArray<FFluidSegmentStateOneD>& SegmentStates)
{
	WaitForStepCompletion();
	FScopeLock WorkerStateLock(&WorkerStateCriticalSection);
	WorkerSegmentStates = SegmentStates;
	RebuildJunctionSceneNodeKeyTopology(WorkerSegmentStates);
	bWorkerStateResident = WorkerSegmentStates.Num() > 0;
}

void FFluidSegment1DCPUSimulation::BakeWorkerStaticStepInputsOnGameThread(UWorld* SimulationWorld, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors)
{
	if (!bBackgroundWorkerEnabled)
	{
		return;
	}

	const float GravityAcceleration = SimulationWorld ? FMath::Abs(SimulationWorld->GetGravityZ()) : 980.0f;
	const FVector GravityDirectionWorld = FVector::UpVector;

	FScopeLock WorkerStateLock(&WorkerStateCriticalSection);
	if (!bWorkerStateResident)
	{
		return;
	}

	SegmentGravityAccelerationAlongAxis.SetNum(WorkerSegmentStates.Num());
	for (int32 SegmentIndex = 0; SegmentIndex < WorkerSegmentStates.Num(); ++SegmentIndex)
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
		SegmentGravityAccelerationAlongAxis[SegmentIndex] = GravityAcceleration * GravityAxisComponent;
	}
}

void FFluidSegment1DCPUSimulation::EnqueueSimulateStepCpuOnly(float SimulationStepTime)
{
	{
		FScopeLock WorkerStateLock(&WorkerStateCriticalSection);
		PendingSimulationStepTimes.Add(SimulationStepTime);
		StepCompletedEvent->Reset();
	}
	EnsureBackgroundWorkerRunning();
	StepRequestedEvent->Trigger();
}

void FFluidSegment1DCPUSimulation::WaitForStepCompletion()
{
	if (!bBackgroundWorkerEnabled || !StepCompletedEvent)
	{
		return;
	}
	StepCompletedEvent->Wait();
}

void FFluidSegment1DCPUSimulation::ReadbackToSegmentStates(TArray<FFluidSegmentStateOneD>& SegmentStates)
{
	WaitForStepCompletion();
	FScopeLock WorkerStateLock(&WorkerStateCriticalSection);
	SegmentStates = WorkerSegmentStates;
}

void FFluidSegment1DCPUSimulation::ReadbackSegmentIndicesToSegmentStates(TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<int32>& SegmentIndices)
{
	WaitForStepCompletion();
	FScopeLock WorkerStateLock(&WorkerStateCriticalSection);
	for (const int32 SegmentIndex : SegmentIndices)
	{
		if (WorkerSegmentStates.IsValidIndex(SegmentIndex) && SegmentStates.IsValidIndex(SegmentIndex))
		{
			SegmentStates[SegmentIndex] = WorkerSegmentStates[SegmentIndex];
		}
	}
}

float FFluidSegment1DCPUSimulation::ComputeMinimumStableStepTimeOnWorker(float RequestedSimulationStepTime)
{
	FScopeLock WorkerStateLock(&WorkerStateCriticalSection);
	float EffectiveStepTime = RequestedSimulationStepTime;
	for (const FFluidSegmentStateOneD& SegmentState : WorkerSegmentStates)
	{
		EffectiveStepTime = FMath::Min(EffectiveStepTime, ComputeStableStepTime(SegmentState));
	}
	return EffectiveStepTime;
}

void FFluidSegment1DCPUSimulation::RunWorkerSimulationStep(float RequestedSimulationStepTime)
{
	FScopeLock WorkerStateLock(&WorkerStateCriticalSection);
	if (!bWorkerStateResident)
	{
		return;
	}
	float EffectiveStepTime = RequestedSimulationStepTime;
	for (const FFluidSegmentStateOneD& SegmentState : WorkerSegmentStates)
	{
		EffectiveStepTime = FMath::Min(EffectiveStepTime, ComputeStableStepTime(SegmentState));
	}
	SimulateStepOnSegmentStates(WorkerSegmentStates, SegmentGravityAccelerationAlongAxis, EffectiveStepTime);
}

void FFluidSegment1DCPUSimulation::ProcessWorkerStepQueueUntilIdle()
{
	for (;;)
	{
		float RequestedSimulationStepTime = 0.0f;
		{
			FScopeLock WorkerStateLock(&WorkerStateCriticalSection);
			if (PendingSimulationStepTimes.Num() == 0)
			{
				StepCompletedEvent->Trigger();
				return;
			}
			RequestedSimulationStepTime = PendingSimulationStepTimes[0];
			PendingSimulationStepTimes.RemoveAt(0, 1, EAllowShrinking::No);
		}
		RunWorkerSimulationStep(RequestedSimulationStepTime);
	}
}

void FFluidSegment1DCPUSimulation::RequestWorkerStop()
{
	bWorkerStopRequested = true;
	if (StepRequestedEvent)
	{
		StepRequestedEvent->Trigger();
	}
}

void FFluidSegment1DCPUSimulation::WorkerThreadMainLoop()
{
	while (!bWorkerStopRequested)
	{
		StepRequestedEvent->Wait();
		if (bWorkerStopRequested)
		{
			break;
		}
		ProcessWorkerStepQueueUntilIdle();
	}
}

void FFluidSegment1DCPUSimulation::StopBackgroundWorker()
{
	RequestWorkerStop();

	if (WorkerThread)
	{
		WorkerThread->WaitForCompletion();
		delete WorkerThread;
		WorkerThread = nullptr;
	}

	if (WorkerRunnable)
	{
		delete WorkerRunnable;
		WorkerRunnable = nullptr;
	}

	bWorkerStopRequested = false;
}

void FFluidSegment1DCPUSimulation::EnsureBackgroundWorkerRunning()
{
	if (!bBackgroundWorkerEnabled)
	{
		return;
	}

	if (!StepRequestedEvent)
	{
		StepRequestedEvent = FPlatformProcess::GetSynchEventFromPool(false);
		StepCompletedEvent = FPlatformProcess::GetSynchEventFromPool(false);
		StepCompletedEvent->Trigger();
	}

	if (WorkerThread)
	{
		return;
	}

	bWorkerStopRequested = false;
	WorkerRunnable = new FFluidSegment1DCpuWorkerRunnable(this);
	WorkerThread = FRunnableThread::Create(WorkerRunnable, TEXT("FluidSegment1DCpuWorker"), 0, TPri_Normal);
}

void FFluidSegment1DCPUSimulation::SimulateStep(UWorld* World, TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors, float SimulationStepTime)
{
	const float GravityAcceleration = World ? FMath::Abs(World->GetGravityZ()) : 980.0f;
	const FVector GravityDirectionWorld = FVector::UpVector;

	UpdateOneDimensionBoundaryFlowsFromAttachedPipePointActors(SegmentStates, SegmentPipeActors);
	RebuildJunctionSceneNodeKeyTopology(SegmentStates);

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

	SimulateStepOnSegmentStates(SegmentStates, GravityAccelerationAlongAxis, SimulationStepTime);
}

void FFluidSegment1DCPUSimulation::SimulateStepOnSegmentStates(TArray<FFluidSegmentStateOneD>& TargetSegmentStates, const TArray<float>& GravityAccelerationAlongAxisPerSegment, float SimulationStepTime)
{
	TArray<FFluidSegmentStateOneD> NextSegmentStates;
	NextSegmentStates.SetNum(TargetSegmentStates.Num());

	const int32 SegmentCount = TargetSegmentStates.Num();
	ParallelFor(SegmentCount, [this, &TargetSegmentStates, &NextSegmentStates, &GravityAccelerationAlongAxisPerSegment, SimulationStepTime](int32 SegmentIndex)
		{
			const FFluidSegmentStateOneD& SegmentState = TargetSegmentStates[SegmentIndex];
			if (SegmentState.CellStates.Num() < 2)
			{
				NextSegmentStates[SegmentIndex] = SegmentState;
				return;
			}

			const float StableStepTime = ComputeStableStepTime(SegmentState);
			const float EffectiveStepTime = FMath::Min(SimulationStepTime, StableStepTime);
			const float GravityAccelerationAlongAxis = GravityAccelerationAlongAxisPerSegment.IsValidIndex(SegmentIndex)
				? GravityAccelerationAlongAxisPerSegment[SegmentIndex]
				: 0.0f;

			FFluidSegmentStateOneD NextSegmentState = SegmentState;
			SolveSegmentWaterHammerStep(SegmentState, EffectiveStepTime, GravityAccelerationAlongAxis, NextSegmentState);
			ApplyBoundaryConditions(SegmentState, NextSegmentState);
			NextSegmentStates[SegmentIndex] = MoveTemp(NextSegmentState);
		});

	ApplyJunctionCouplingToNextSegmentStates(TargetSegmentStates, NextSegmentStates);

	for (int32 SegmentIndex = 0; SegmentIndex < TargetSegmentStates.Num(); ++SegmentIndex)
	{
		FFluidSegmentStateOneD& SegmentState = TargetSegmentStates[SegmentIndex];
		FFluidSegmentStateOneD& NextSegmentState = NextSegmentStates[SegmentIndex];
		UpdateDerivedCellValues(NextSegmentState);
		if (!IsSegmentStateFinite(NextSegmentState))
		{
			continue;
		}
		SegmentState = MoveTemp(NextSegmentState);
	}

	const ULazyFluidPipesDeveloperSettings& Settings = BoundSimulationSettings
		? *BoundSimulationSettings
		: FFluidPipesSimulationSettingsLibrary::ResolveSimulationSettings(nullptr);
	FFluidSimulationStateLimits::ClampAllSegmentStatesOneD(TargetSegmentStates, Settings);
}

void FFluidSegment1DCPUSimulation::UpdateOneDimensionBoundaryFlowsFromAttachedPipePointActors(TArray<FFluidSegmentStateOneD>& TargetSegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors) const
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

void FFluidSegment1DCPUSimulation::RebuildJunctionSceneNodeKeyTopology(const TArray<FFluidSegmentStateOneD>& SourceSegmentStates)
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
			JunctionSceneNodeKeyToIncidentEndpoints.FindOrAdd(SegmentState.LeftSceneNodeKey).Add(FFluidOneDJunctionEndpointIncident{ SegmentIndex, true });
		}
		if (SegmentState.RightSceneNodeKey != INDEX_NONE)
		{
			JunctionSceneNodeKeyToIncidentEndpoints.FindOrAdd(SegmentState.RightSceneNodeKey).Add(FFluidOneDJunctionEndpointIncident{ SegmentIndex, false });
		}
	}
}

void FFluidSegment1DCPUSimulation::ApplyJunctionCouplingToNextSegmentStates(const TArray<FFluidSegmentStateOneD>& CurrentSegmentStates, TArray<FFluidSegmentStateOneD>& NextSegmentStates) const
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

void FFluidSegment1DCPUSimulation::SolveSegmentWaterHammerStep(const FFluidSegmentStateOneD& CurrentSegmentState, float SimulationStepTime, float GravityAccelerationAlongAxis, FFluidSegmentStateOneD& NextSegmentState) const
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

void FFluidSegment1DCPUSimulation::ApplyBoundaryConditions(const FFluidSegmentStateOneD& CurrentSegmentState, FFluidSegmentStateOneD& NextSegmentState) const
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

void FFluidSegment1DCPUSimulation::UpdateDerivedCellValues(FFluidSegmentStateOneD& SegmentState) const
{
	const float CrossSectionArea = GetCrossSectionArea(SegmentState);
	for (FFluidSegmentCellStateOneD& CellState : SegmentState.CellStates)
	{
		CellState.Velocity = CellState.FlowRate / FMath::Max(CrossSectionArea, KINDA_SMALL_NUMBER);
	}
}

float FFluidSegment1DCPUSimulation::ComputeStableStepTime(const FFluidSegmentStateOneD& SegmentState) const
{
	const ULazyFluidPipesDeveloperSettings& Settings = BoundSimulationSettings
		? *BoundSimulationSettings
		: FFluidPipesSimulationSettingsLibrary::ResolveSimulationSettings(nullptr);
	const float SafeWaveSpeed = FMath::Max(SegmentState.WaveSpeed, 1.0f);
	const float SafeCellLength = FMath::Max(SegmentState.CellLength, 0.01f);
	return Settings.OneDSolverCflFactor * SafeCellLength / SafeWaveSpeed;
}

float FFluidSegment1DCPUSimulation::GetCrossSectionArea(const FFluidSegmentStateOneD& SegmentState) const
{
	const float SafePipeDiameter = FMath::Max(SegmentState.PipeDiameter, 0.001f);
	return PI * 0.25f * SafePipeDiameter * SafePipeDiameter;
}

bool FFluidSegment1DCPUSimulation::IsSegmentStateFinite(const FFluidSegmentStateOneD& SegmentState) const
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
