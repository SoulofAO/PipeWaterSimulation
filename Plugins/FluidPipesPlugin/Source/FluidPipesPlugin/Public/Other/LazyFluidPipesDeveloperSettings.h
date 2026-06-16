#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Other/FluidPipesSimulationSettingsTypes.h"
#include "LazyFluidPipesDeveloperSettings.generated.h"

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced, config = FluidPipesPlugin, defaultconfig)
class FLUIDPIPESPLUGIN_API ULazyFluidPipesDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidBenchmarkProfile")
	FName ProfileLabel = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidZeroD")
	bool EnableFluidNetworkSimulationZeroD = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidZeroD")
	bool ZeroDMergeColinearPassiveJunctionAtImport = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidZeroD")
	bool EnableZeroDSimulationStateVariableClamping = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidZeroD")
	float ZeroDMinimumPressure = -1000000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidZeroD")
	float ZeroDMaximumPressure = 1000000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidZeroD", meta = (ClampMin = "0.000001", UIMin = "0.000001"))
	float ZeroDPressureScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidZeroD")
	float ZeroDMinimumVolumeFlowRate = -10000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidZeroD")
	float ZeroDMaximumVolumeFlowRate = 10000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidZeroD", meta = (ClampMin = "0.001", UIMin = "0.001"))
	float SimulationStepTimeZeroD = 0.016f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidOneD")
	bool EnableFluidSegmentSimulationOneD = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidOneD")
	bool OneDMergeColinearPassiveJunctionAtImport = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidOneD")
	bool EnableOneDSimulationStateVariableClamping = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidOneD")
	float OneDMinimumPressure = -1000000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidOneD")
	float OneDMaximumPressure = 1000000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidOneD")
	float OneDMinimumVolumeFlowRate = -10000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidOneD")
	float OneDMaximumVolumeFlowRate = 10000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidOneD")
	float OneDMinimumVelocity = -100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidOneD")
	float OneDMaximumVelocity = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidOneD", meta = (ClampMin = "0.001", UIMin = "0.001"))
	float SimulationStepTimeOneD = 0.008f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidOneD", meta = (ClampMin = "0.1", ClampMax = "1.0", UIMin = "0.1", UIMax = "1.0"))
	float OneDSolverCflFactor = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidOneD")
	EFluidSegmentSimulationOneDBackend FluidSegmentSimulationOneDBackend = EFluidSegmentSimulationOneDBackend::CpuGameThread;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidLevelImport")
	EFluidLevelPipeImportTarget LevelPipeImportTarget = EFluidLevelPipeImportTarget::Disabled;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidPipesWorldDebug", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float WorldDebugMaximumDrawDistanceCentimeters = 4000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidPipesWorldDebug")
	bool WorldDebugPerspectiveFontScaling = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidPipesWorldDebug", meta = (ClampMin = "50.0", UIMin = "50.0"))
	float WorldDebugPerspectiveFontReferenceDistanceCentimeters = 450.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidPipesWorldDebug", meta = (ClampMin = "0.05", UIMin = "0.05", ClampMax = "1.0", UIMax = "1.0"))
	float WorldDebugPerspectiveFontMinimumMultiplier = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidPipesWorldDebug", meta = (ClampMin = "1.0", UIMin = "1.0", ClampMax = "6.0", UIMax = "6.0"))
	float WorldDebugPerspectiveFontMaximumMultiplier = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidPipesWorldDebug")
	bool WorldDebugIncludeOneDWireGeometry = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidPipesWorldDebug")
	bool WorldDebugIncludeOneDSegmentSummary = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidPipesWorldDebug")
	bool WorldDebugIncludeOneDEndpointCaptions = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidPipesWorldDebug")
	bool WorldDebugIncludeOneDPerCellCaptions = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidPipesWorldDebug")
	bool WorldDebugIncludeZeroDWireGeometry = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidPipesWorldDebug")
	bool WorldDebugIncludeZeroDNodeCaptions = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FluidPipesWorldDebug")
	bool WorldDebugIncludeZeroDFlowArrows = true;

	void CopyFrom(const ULazyFluidPipesDeveloperSettings& SourceSettings);
	FString BuildProfileDescription() const;
};
