// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LazyFluidPipesDeveloperSettings.generated.h"

UENUM(BlueprintType)
enum class EFluidLevelPipeImportTarget : uint8
{
	Disabled,
	ZeroDNetwork,
	OneDSegments,
	Both
};

UCLASS(config = FluidPipesPlugin, defaultconfig)
class FLUIDPIPESPLUGIN_API ULazyFluidPipesDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Config, Category = "FluidZeroD")
	bool EnableFluidNetworkSimulationZeroD = true;

	UPROPERTY(EditAnywhere, Config, Category = "FluidZeroD", meta = (ClampMin = "0.001", UIMin = "0.001"))
	float SimulationStepTimeZeroD = 0.016f;

	UPROPERTY(EditAnywhere, Config, Category = "FluidOneD")
	bool EnableFluidSegmentSimulationOneD = true;

	UPROPERTY(EditAnywhere, Config, Category = "FluidOneD", meta = (ClampMin = "0.001", UIMin = "0.001"))
	float SimulationStepTimeOneD = 0.008f;

	UPROPERTY(EditAnywhere, Config, Category = "FluidOneD", meta = (ClampMin = "0.1", ClampMax = "1.0", UIMin = "0.1", UIMax = "1.0"))
	float OneDSolverCflFactor = 0.9f;

	UPROPERTY(EditAnywhere, Config, Category = "FluidLevelImport")
	EFluidLevelPipeImportTarget LevelPipeImportTarget = EFluidLevelPipeImportTarget::Disabled;

	UPROPERTY(EditAnywhere, Config, Category = "FluidPipesWorldDebug", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float WorldDebugMaximumDrawDistanceCentimeters = 4000.0f;

	UPROPERTY(EditAnywhere, Config, Category = "FluidPipesWorldDebug")
	bool WorldDebugPerspectiveFontScaling = true;

	UPROPERTY(EditAnywhere, Config, Category = "FluidPipesWorldDebug", meta = (ClampMin = "50.0", UIMin = "50.0"))
	float WorldDebugPerspectiveFontReferenceDistanceCentimeters = 450.0f;

	UPROPERTY(EditAnywhere, Config, Category = "FluidPipesWorldDebug", meta = (ClampMin = "0.05", UIMin = "0.05", ClampMax = "1.0", UIMax = "1.0"))
	float WorldDebugPerspectiveFontMinimumMultiplier = 0.35f;

	UPROPERTY(EditAnywhere, Config, Category = "FluidPipesWorldDebug", meta = (ClampMin = "1.0", UIMin = "1.0", ClampMax = "6.0", UIMax = "6.0"))
	float WorldDebugPerspectiveFontMaximumMultiplier = 2.5f;

	UPROPERTY(EditAnywhere, Config, Category = "FluidPipesWorldDebug")
	bool WorldDebugIncludeOneDWireGeometry = true;

	UPROPERTY(EditAnywhere, Config, Category = "FluidPipesWorldDebug")
	bool WorldDebugIncludeOneDSegmentSummary = true;

	UPROPERTY(EditAnywhere, Config, Category = "FluidPipesWorldDebug")
	bool WorldDebugIncludeOneDEndpointCaptions = true;

	UPROPERTY(EditAnywhere, Config, Category = "FluidPipesWorldDebug")
	bool WorldDebugIncludeOneDPerCellCaptions = true;

	UPROPERTY(EditAnywhere, Config, Category = "FluidPipesWorldDebug")
	bool WorldDebugIncludeZeroDWireGeometry = true;

	UPROPERTY(EditAnywhere, Config, Category = "FluidPipesWorldDebug")
	bool WorldDebugIncludeZeroDNodeCaptions = true;

	UPROPERTY(EditAnywhere, Config, Category = "FluidPipesWorldDebug")
	bool WorldDebugIncludeZeroDFlowArrows = true;
};
