#pragma once

#include "CoreMinimal.h"

class APipeFluidPipeActor;

struct FFluidPipeLumpedPhysicsProperties
{
	float CrossSectionAreaSquareMeters = 0.0f;
	float FrictionQuadraticCoefficient = 0.0f;
	float PipeFluidCompliance = 0.0f;
	float PipeInertance = 0.0f;
};

struct FFluidPipeLumpedPhysicsLibrary
{
	static constexpr float CentimetersPerMeter = 100.0f;
	static constexpr float MinimumOneDimensionCellLengthMeters = 0.0001f;

	static float ConvertSegmentLengthCentimetersToMeters(float SegmentLengthCentimeters);

	static float ConvertGravityAccelerationCentimetersPerSecondSquaredToMetersPerSecondSquared(float GravityAccelerationCentimetersPerSecondSquared);

	static float ComputeOneDimensionCellLengthMeters(float CellLengthCentimeters);

	static float ComputeOneDimensionStableStepTimeSeconds(float CellLengthCentimeters, float WaveSpeedMetersPerSecond, float SolverCflFactor);

	static float ComputeCrossSectionAreaSquareMeters(float PipeDiameterMeters);

	static FFluidPipeLumpedPhysicsProperties DeriveLumpedPropertiesFromPipeActor(const APipeFluidPipeActor& PipeActor);

	static FFluidPipeLumpedPhysicsProperties DeriveLumpedProperties(
		float SegmentLengthCentimeters,
		float PipeDiameterMeters,
		float DensityKilogramsPerCubicMeter,
		float WaveSpeedMetersPerSecond,
		float FrictionFactor);

	static float ComputeVolumeFlowRateFromQuadraticPressureDrop(float PressureDropPascals, float FrictionQuadraticCoefficient);

	static float ComputeEffectiveLinearResistanceFromQuadraticCoefficient(float FrictionQuadraticCoefficient, float VolumeFlowRateCubicMetersPerSecond);
};
