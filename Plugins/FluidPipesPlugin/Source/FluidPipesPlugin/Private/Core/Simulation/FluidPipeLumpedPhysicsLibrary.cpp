#include "Core/Simulation/FluidPipeLumpedPhysicsLibrary.h"

#include "Core/Actors/PipeFluidPipeActor.h"

float FFluidPipeLumpedPhysicsLibrary::ConvertSegmentLengthCentimetersToMeters(float SegmentLengthCentimeters)
{
	return FMath::Max(SegmentLengthCentimeters, 0.0f) / CentimetersPerMeter;
}

float FFluidPipeLumpedPhysicsLibrary::ConvertGravityAccelerationCentimetersPerSecondSquaredToMetersPerSecondSquared(float GravityAccelerationCentimetersPerSecondSquared)
{
	return FMath::Max(GravityAccelerationCentimetersPerSecondSquared, 0.0f) / CentimetersPerMeter;
}

float FFluidPipeLumpedPhysicsLibrary::ComputeOneDimensionCellLengthMeters(float CellLengthCentimeters)
{
	return FMath::Max(ConvertSegmentLengthCentimetersToMeters(CellLengthCentimeters), MinimumOneDimensionCellLengthMeters);
}

float FFluidPipeLumpedPhysicsLibrary::ComputeOneDimensionStableStepTimeSeconds(float CellLengthCentimeters, float WaveSpeedMetersPerSecond, float SolverCflFactor)
{
	const float SafeWaveSpeedMetersPerSecond = FMath::Max(WaveSpeedMetersPerSecond, 1.0f);
	const float CellLengthMeters = ComputeOneDimensionCellLengthMeters(CellLengthCentimeters);
	return FMath::Clamp(SolverCflFactor, 0.1f, 1.0f) * CellLengthMeters / SafeWaveSpeedMetersPerSecond;
}

float FFluidPipeLumpedPhysicsLibrary::ComputeCrossSectionAreaSquareMeters(float PipeDiameterMeters)
{
	const float SafePipeDiameterMeters = FMath::Max(PipeDiameterMeters, 0.001f);
	return PI * 0.25f * SafePipeDiameterMeters * SafePipeDiameterMeters;
}

FFluidPipeLumpedPhysicsProperties FFluidPipeLumpedPhysicsLibrary::DeriveLumpedPropertiesFromPipeActor(const APipeFluidPipeActor& PipeActor)
{
	return DeriveLumpedProperties(
		PipeActor.SegmentPhysicsLength,
		PipeActor.PipeDiameter,
		PipeActor.Density,
		PipeActor.WaveSpeed,
		PipeActor.FrictionFactor);
}

FFluidPipeLumpedPhysicsProperties FFluidPipeLumpedPhysicsLibrary::DeriveLumpedProperties(
	float SegmentLengthCentimeters,
	float PipeDiameterMeters,
	float DensityKilogramsPerCubicMeter,
	float WaveSpeedMetersPerSecond,
	float FrictionFactor)
{
	FFluidPipeLumpedPhysicsProperties Properties;
	const float SegmentLengthMeters = ConvertSegmentLengthCentimetersToMeters(SegmentLengthCentimeters);
	const float CrossSectionAreaSquareMeters = ComputeCrossSectionAreaSquareMeters(PipeDiameterMeters);
	const float SafeDensityKilogramsPerCubicMeter = FMath::Max(DensityKilogramsPerCubicMeter, KINDA_SMALL_NUMBER);
	const float SafeWaveSpeedMetersPerSecond = FMath::Max(WaveSpeedMetersPerSecond, 1.0f);
	const float SafePipeDiameterMeters = FMath::Max(PipeDiameterMeters, 0.001f);

	Properties.CrossSectionAreaSquareMeters = CrossSectionAreaSquareMeters;
	Properties.FrictionQuadraticCoefficient = FrictionFactor * SegmentLengthMeters * SafeDensityKilogramsPerCubicMeter
		/ (2.0f * SafePipeDiameterMeters * CrossSectionAreaSquareMeters * CrossSectionAreaSquareMeters);
	Properties.PipeFluidCompliance = SegmentLengthMeters * CrossSectionAreaSquareMeters
		/ (SafeDensityKilogramsPerCubicMeter * SafeWaveSpeedMetersPerSecond * SafeWaveSpeedMetersPerSecond);
	Properties.PipeInertance = SafeDensityKilogramsPerCubicMeter * SegmentLengthMeters / FMath::Max(CrossSectionAreaSquareMeters, KINDA_SMALL_NUMBER);

	return Properties;
}

float FFluidPipeLumpedPhysicsLibrary::ComputeVolumeFlowRateFromQuadraticPressureDrop(float PressureDropPascals, float FrictionQuadraticCoefficient)
{
	const float SafeFrictionQuadraticCoefficient = FMath::Max(FrictionQuadraticCoefficient, KINDA_SMALL_NUMBER);
	const float PressureDropMagnitude = FMath::Abs(PressureDropPascals);
	const float FlowMagnitude = FMath::Sqrt(PressureDropMagnitude / SafeFrictionQuadraticCoefficient);
	return FMath::Sign(PressureDropPascals) * FlowMagnitude;
}

float FFluidPipeLumpedPhysicsLibrary::ComputeEffectiveLinearResistanceFromQuadraticCoefficient(float FrictionQuadraticCoefficient, float VolumeFlowRateCubicMetersPerSecond)
{
	const float SafeFrictionQuadraticCoefficient = FMath::Max(FrictionQuadraticCoefficient, KINDA_SMALL_NUMBER);
	const float FlowMagnitude = FMath::Max(FMath::Abs(VolumeFlowRateCubicMetersPerSecond), KINDA_SMALL_NUMBER);
	return 2.0f * SafeFrictionQuadraticCoefficient * FlowMagnitude;
}
