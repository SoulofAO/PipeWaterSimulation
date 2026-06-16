#include "Other/LazyFluidPipesDeveloperSettings.h"

void ULazyFluidPipesDeveloperSettings::CopyFrom(const ULazyFluidPipesDeveloperSettings& SourceSettings)
{
	ProfileLabel = SourceSettings.ProfileLabel;
	EnableFluidNetworkSimulationZeroD = SourceSettings.EnableFluidNetworkSimulationZeroD;
	ZeroDMergeColinearPassiveJunctionAtImport = SourceSettings.ZeroDMergeColinearPassiveJunctionAtImport;
	ZeroDAutoDeriveLumpedParametersFromPipePhysics = SourceSettings.ZeroDAutoDeriveLumpedParametersFromPipePhysics;
	ZeroDFixedPointIterationCount = SourceSettings.ZeroDFixedPointIterationCount;
	ZeroDSubstepSafetyFactor = SourceSettings.ZeroDSubstepSafetyFactor;
	EnableZeroDSimulationStateVariableClamping = SourceSettings.EnableZeroDSimulationStateVariableClamping;
	ZeroDMinimumPressure = SourceSettings.ZeroDMinimumPressure;
	ZeroDMaximumPressure = SourceSettings.ZeroDMaximumPressure;
	ZeroDPressureScale = SourceSettings.ZeroDPressureScale;
	ZeroDMinimumVolumeFlowRate = SourceSettings.ZeroDMinimumVolumeFlowRate;
	ZeroDMaximumVolumeFlowRate = SourceSettings.ZeroDMaximumVolumeFlowRate;
	SimulationStepTimeZeroD = SourceSettings.SimulationStepTimeZeroD;
	EnableFluidSegmentSimulationOneD = SourceSettings.EnableFluidSegmentSimulationOneD;
	OneDMergeColinearPassiveJunctionAtImport = SourceSettings.OneDMergeColinearPassiveJunctionAtImport;
	EnableOneDSimulationStateVariableClamping = SourceSettings.EnableOneDSimulationStateVariableClamping;
	OneDMinimumPressure = SourceSettings.OneDMinimumPressure;
	OneDMaximumPressure = SourceSettings.OneDMaximumPressure;
	OneDMinimumVolumeFlowRate = SourceSettings.OneDMinimumVolumeFlowRate;
	OneDMaximumVolumeFlowRate = SourceSettings.OneDMaximumVolumeFlowRate;
	OneDMinimumVelocity = SourceSettings.OneDMinimumVelocity;
	OneDMaximumVelocity = SourceSettings.OneDMaximumVelocity;
	SimulationStepTimeOneD = SourceSettings.SimulationStepTimeOneD;
	OneDSolverCflFactor = SourceSettings.OneDSolverCflFactor;
	FluidSegmentSimulationOneDBackend = SourceSettings.FluidSegmentSimulationOneDBackend;
	LevelPipeImportTarget = SourceSettings.LevelPipeImportTarget;
	WorldDebugMaximumDrawDistanceCentimeters = SourceSettings.WorldDebugMaximumDrawDistanceCentimeters;
	WorldDebugPerspectiveFontScaling = SourceSettings.WorldDebugPerspectiveFontScaling;
	WorldDebugPerspectiveFontReferenceDistanceCentimeters = SourceSettings.WorldDebugPerspectiveFontReferenceDistanceCentimeters;
	WorldDebugPerspectiveFontMinimumMultiplier = SourceSettings.WorldDebugPerspectiveFontMinimumMultiplier;
	WorldDebugPerspectiveFontMaximumMultiplier = SourceSettings.WorldDebugPerspectiveFontMaximumMultiplier;
	WorldDebugIncludeOneDWireGeometry = SourceSettings.WorldDebugIncludeOneDWireGeometry;
	WorldDebugIncludeOneDSegmentSummary = SourceSettings.WorldDebugIncludeOneDSegmentSummary;
	WorldDebugIncludeOneDEndpointCaptions = SourceSettings.WorldDebugIncludeOneDEndpointCaptions;
	WorldDebugIncludeOneDPerCellCaptions = SourceSettings.WorldDebugIncludeOneDPerCellCaptions;
	WorldDebugIncludeZeroDWireGeometry = SourceSettings.WorldDebugIncludeZeroDWireGeometry;
	WorldDebugIncludeZeroDNodeCaptions = SourceSettings.WorldDebugIncludeZeroDNodeCaptions;
	WorldDebugIncludeZeroDFlowArrows = SourceSettings.WorldDebugIncludeZeroDFlowArrows;
}

FString ULazyFluidPipesDeveloperSettings::BuildProfileDescription() const
{
	if (!ProfileLabel.IsNone())
	{
		return ProfileLabel.ToString();
	}
	return FString::Format(
		TEXT("0D={0} 1D={1} Backend={2}"),
		{
			EnableFluidNetworkSimulationZeroD ? TEXT("on") : TEXT("off"),
			EnableFluidSegmentSimulationOneD ? TEXT("on") : TEXT("off"),
			UEnum::GetDisplayValueAsText(FluidSegmentSimulationOneDBackend).ToString()
		});
}
