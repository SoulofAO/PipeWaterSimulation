#include "Core/Simulation1D/FluidSegment1DCPUSimulation.h"

#include "Core/Actors/PipeFluidBasePointActor.h"
#include "Core/Simulation/FluidSimulationStateLimits.h"
#include "Core/Actors/PipeFluidPipeActor.h"
#include "Core/Simulation1D/FluidSegment1DSimulationLibrary.h"
#include "Engine/World.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Other/LazyFluidPipesDeveloperSettings.h"

bool FFluidSegment1DCPUSimulation::IsAvailable() const
{
	return true;
}

void FFluidSegment1DCPUSimulation::Release()
{
	JunctionSceneNodeKeyToIncidentEndpoints.Reset();
}

void FFluidSegment1DCPUSimulation::RebuildFromSegments(const TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>&)
{
	RebuildJunctionSceneNodeKeyTopology(SegmentStates);
}

void FFluidSegment1DCPUSimulation::SimulateStep(UWorld* World, TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors, float SimulationStepTime, bool bWaitForReadbackBeforeLock)
{
	const float GravityAcceleration = World ? FMath::Abs(World->GetGravityZ()) : 980.0f;
	const FVector GravityDirectionWorld = FVector::UpVector;

	UpdateOneDimensionBoundaryFlowsFromAttachedPipePointActors(SegmentStates, SegmentPipeActors);
	RebuildJunctionSceneNodeKeyTopology(SegmentStates);

	TArray<FFluidSegmentStateOneD> NextSegmentStates;
	NextSegmentStates.SetNum(SegmentStates.Num());
	for (int32 SegmentIndex = 0; SegmentIndex < SegmentStates.Num(); ++SegmentIndex)
	{
		FFluidSegmentStateOneD& SegmentState = SegmentStates[SegmentIndex];
		if (SegmentState.CellStates.Num() < 2)
		{
			NextSegmentStates[SegmentIndex] = SegmentState;
			continue;
		}

		const float StableStepTime = ComputeStableStepTime(SegmentState);
		const float EffectiveStepTime = FMath::Min(SimulationStepTime, StableStepTime);

		float GravityAxisComponent = 0.0f;
		if (SegmentPipeActors.IsValidIndex(SegmentIndex))
		{
			const APipeFluidPipeActor* PipeActor = SegmentPipeActors[SegmentIndex].Get();
			if (PipeActor)
			{
				const FVector PipeAxisDirectionWorld = PipeActor->GetActorForwardVector().GetSafeNormal();
				GravityAxisComponent = FVector::DotProduct(GravityDirectionWorld, PipeAxisDirectionWorld);
			}
		}

		const float GravityAccelerationAlongAxis = GravityAcceleration * GravityAxisComponent;
		FFluidSegmentStateOneD NextSegmentState = SegmentState;
		SolveSegmentWaterHammerStep(SegmentState, EffectiveStepTime, GravityAccelerationAlongAxis, NextSegmentState);
		ApplyBoundaryConditions(SegmentState, NextSegmentState);
		NextSegmentStates[SegmentIndex] = MoveTemp(NextSegmentState);
	}

	ApplyJunctionCouplingToNextSegmentStates(SegmentStates, NextSegmentStates);

	for (int32 SegmentIndex = 0; SegmentIndex < SegmentStates.Num(); ++SegmentIndex)
	{
		FFluidSegmentStateOneD& SegmentState = SegmentStates[SegmentIndex];
		FFluidSegmentStateOneD& NextSegmentState = NextSegmentStates[SegmentIndex];
		UpdateDerivedCellValues(NextSegmentState);
		if (!IsSegmentStateFinite(NextSegmentState))
		{
			continue;
		}
		SegmentState = MoveTemp(NextSegmentState);
	}

	const ULazyFluidPipesDeveloperSettings* Settings = GetDefault<ULazyFluidPipesDeveloperSettings>();
	FFluidSimulationStateLimits::ClampAllSegmentStatesOneD(SegmentStates, *Settings);
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
	const ULazyFluidPipesDeveloperSettings* Settings = GetDefault<ULazyFluidPipesDeveloperSettings>();
	const float SafeWaveSpeed = FMath::Max(SegmentState.WaveSpeed, 1.0f);
	const float SafeCellLength = FMath::Max(SegmentState.CellLength, 0.01f);
	return Settings->OneDSolverCflFactor * SafeCellLength / SafeWaveSpeed;
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
