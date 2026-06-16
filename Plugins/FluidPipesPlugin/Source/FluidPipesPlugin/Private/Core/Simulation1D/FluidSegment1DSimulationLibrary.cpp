#include "Core/Simulation1D/FluidSegment1DSimulationLibrary.h"

#include "Core/Simulation/FluidPipeLumpedPhysicsLibrary.h"

void FFluidSegment1DSimulationLibrary::AdvanceInteriorWaterHammerLaxFriedrichsStep(
	const FFluidSegmentStateOneD& CurrentSegmentState,
	float SimulationStepTime,
	float GravityAccelerationAlongAxisMetersPerSecondSquared,
	FFluidSegmentStateOneD& NextSegmentState)
{
	const float SafePipeDiameterMeters = FMath::Max(CurrentSegmentState.PipeDiameter, 0.001f);
	const float CrossSectionAreaSquareMeters = FFluidPipeLumpedPhysicsLibrary::ComputeCrossSectionAreaSquareMeters(SafePipeDiameterMeters);
	const float SafeDensityKilogramsPerCubicMeter = FMath::Max(CurrentSegmentState.Density, KINDA_SMALL_NUMBER);
	const float SafeWaveSpeedMetersPerSecond = FMath::Max(CurrentSegmentState.WaveSpeed, 1.0f);
	const float SafeCellLengthMeters = FFluidPipeLumpedPhysicsLibrary::ComputeOneDimensionCellLengthMeters(CurrentSegmentState.CellLength);
	const float FrictionResistance = CurrentSegmentState.FrictionFactor
		/ (2.0f * SafePipeDiameterMeters * FMath::Max(CrossSectionAreaSquareMeters, KINDA_SMALL_NUMBER));
	const float PressureCoefficient = SafeDensityKilogramsPerCubicMeter * SafeWaveSpeedMetersPerSecond * SafeWaveSpeedMetersPerSecond
		/ FMath::Max(CrossSectionAreaSquareMeters, KINDA_SMALL_NUMBER);
	const float FlowCoefficient = CrossSectionAreaSquareMeters / SafeDensityKilogramsPerCubicMeter;
	const float GravitySourceTerm = -CrossSectionAreaSquareMeters * GravityAccelerationAlongAxisMetersPerSecondSquared;
	const float PressureCouplingFactor = PressureCoefficient * SimulationStepTime / (2.0f * SafeCellLengthMeters);
	const float FlowCouplingFactor = FlowCoefficient * SimulationStepTime / (2.0f * SafeCellLengthMeters);

	NextSegmentState.CellStates = CurrentSegmentState.CellStates;
	for (int32 CellIndex = 1; CellIndex < CurrentSegmentState.CellStates.Num() - 1; ++CellIndex)
	{
		const float LeftFlow = CurrentSegmentState.CellStates[CellIndex - 1].FlowRate;
		const float CenterFlow = CurrentSegmentState.CellStates[CellIndex].FlowRate;
		const float RightFlow = CurrentSegmentState.CellStates[CellIndex + 1].FlowRate;
		const float LeftPressure = CurrentSegmentState.CellStates[CellIndex - 1].Pressure;
		const float RightPressure = CurrentSegmentState.CellStates[CellIndex + 1].Pressure;
		const float FrictionSourceTerm = -FrictionResistance * CenterFlow * FMath::Abs(CenterFlow) + GravitySourceTerm;

		NextSegmentState.CellStates[CellIndex].Pressure = 0.5f * (LeftPressure + RightPressure) - PressureCouplingFactor * (RightFlow - LeftFlow);
		NextSegmentState.CellStates[CellIndex].FlowRate = 0.5f * (LeftFlow + RightFlow) - FlowCouplingFactor * (RightPressure - LeftPressure)
			+ SimulationStepTime * FrictionSourceTerm;
	}
}
