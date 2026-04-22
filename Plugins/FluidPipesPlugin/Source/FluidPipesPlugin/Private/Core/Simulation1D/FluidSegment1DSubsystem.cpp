#include "Core/Simulation1D/FluidSegment1DSubsystem.h"

#include "Kismet/KismetSystemLibrary.h"
#include "Other/LazyFluidPipesDeveloperSettings.h"

void UFluidSegment1DSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ResetSimulationState();
}

void UFluidSegment1DSubsystem::Deinitialize()
{
	ResetSimulationState();
	Super::Deinitialize();
}

void UFluidSegment1DSubsystem::Tick(float DeltaTime)
{
	const ULazyFluidPipesDeveloperSettings* Settings = GetDefault<ULazyFluidPipesDeveloperSettings>();
	if (!Settings->EnableFluidSegmentSimulationOneD)
	{
		return;
	}

	AccumulatedTime += DeltaTime;
	while (AccumulatedTime >= Settings->SimulationStepTimeOneD)
	{
		SimulateStep(Settings->SimulationStepTimeOneD);
		AccumulatedTime -= Settings->SimulationStepTimeOneD;
	}

	if (Settings->EnableFluidDebugMessages)
	{
		UKismetSystemLibrary::PrintString(this, FString::Format(TEXT("1D Tick: Segments={0}"), { SegmentStates.Num() }), true, false, FLinearColor::Cyan, 0.0f);
	}
}

TStatId UFluidSegment1DSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFluidSegment1DSubsystem, STATGROUP_Tickables);
}

void UFluidSegment1DSubsystem::ResetSimulationState()
{
	SegmentStates.Reset();
	AccumulatedTime = 0.0f;
}

const TArray<FFluidSegmentStateOneD>& UFluidSegment1DSubsystem::GetSegmentStates() const
{
	return SegmentStates;
}

void UFluidSegment1DSubsystem::SimulateStep(float SimulationStepTime)
{
	const ULazyFluidPipesDeveloperSettings* Settings = GetDefault<ULazyFluidPipesDeveloperSettings>();
	for (FFluidSegmentStateOneD& SegmentState : SegmentStates)
	{
		if (SegmentState.CellStates.Num() < 2)
		{
			continue;
		}

		const float StableStepTime = ComputeStableStepTime(SegmentState);
		const float EffectiveStepTime = FMath::Min(SimulationStepTime, StableStepTime);
		if (Settings->EnableFluidDebugMessages && SimulationStepTime > StableStepTime + KINDA_SMALL_NUMBER)
		{
			UKismetSystemLibrary::PrintString(this, FString::Format(TEXT("1D CFL Limited: Requested={0}, Stable={1}"), { SimulationStepTime, StableStepTime }), true, false, FLinearColor::Yellow, 0.0f);
		}

		FFluidSegmentStateOneD NextSegmentState = SegmentState;
		SolveSegmentWaterHammerStep(SegmentState, EffectiveStepTime, NextSegmentState);
		ApplyBoundaryConditions(SegmentState, NextSegmentState);
		UpdateDerivedCellValues(NextSegmentState);
		if (!IsSegmentStateFinite(NextSegmentState))
		{
			continue;
		}
		SegmentState = MoveTemp(NextSegmentState);
	}
}

void UFluidSegment1DSubsystem::SolveSegmentWaterHammerStep(const FFluidSegmentStateOneD& CurrentSegmentState, float SimulationStepTime, FFluidSegmentStateOneD& NextSegmentState) const
{
	const float CrossSectionArea = GetCrossSectionArea(CurrentSegmentState);
	const float SafeDensity = FMath::Max(CurrentSegmentState.Density, KINDA_SMALL_NUMBER);
	const float SafeWaveSpeed = FMath::Max(CurrentSegmentState.WaveSpeed, 1.0f);
	const float SafeCellLength = FMath::Max(CurrentSegmentState.CellLength, 0.01f);
	const float FrictionResistance = CurrentSegmentState.FrictionFactor / (2.0f * FMath::Max(CurrentSegmentState.PipeDiameter, 0.001f) * FMath::Max(CrossSectionArea, KINDA_SMALL_NUMBER));
	const float PressureCoefficient = SafeDensity * SafeWaveSpeed * SafeWaveSpeed / FMath::Max(CrossSectionArea, KINDA_SMALL_NUMBER);

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
		const float FlowDerivative = -(CrossSectionArea / SafeDensity) * PressureGradient - FrictionResistance * CenterFlow * FMath::Abs(CenterFlow);
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
	const float SafeDensity = FMath::Max(SegmentState.Density, KINDA_SMALL_NUMBER);
	const float ReferencePressure = SegmentState.CellStates.Num() > 0 ? SegmentState.CellStates[0].Pressure : 0.0f;
	const float PressureScale = SafeDensity * FMath::Max(SegmentState.WaveSpeed * SegmentState.WaveSpeed, 1.0f);
	for (FFluidSegmentCellStateOneD& CellState : SegmentState.CellStates)
	{
		CellState.Velocity = CellState.FlowRate / FMath::Max(CrossSectionArea, KINDA_SMALL_NUMBER);
		CellState.FillRatio = FMath::Clamp(0.5f + (CellState.Pressure - ReferencePressure) / PressureScale, 0.0f, 1.0f);
	}
}

float UFluidSegment1DSubsystem::ComputeStableStepTime(const FFluidSegmentStateOneD& SegmentState) const
{
	const ULazyFluidPipesDeveloperSettings* Settings = GetDefault<ULazyFluidPipesDeveloperSettings>();
	const float SafeWaveSpeed = FMath::Max(SegmentState.WaveSpeed, 1.0f);
	const float SafeCellLength = FMath::Max(SegmentState.CellLength, 0.01f);
	return Settings->OneDSolverCflFactor * SafeCellLength / SafeWaveSpeed;
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
		if (!FMath::IsFinite(CellState.Pressure) || !FMath::IsFinite(CellState.FlowRate) || !FMath::IsFinite(CellState.Velocity) || !FMath::IsFinite(CellState.FillRatio))
		{
			return false;
		}
	}

	return true;
}
