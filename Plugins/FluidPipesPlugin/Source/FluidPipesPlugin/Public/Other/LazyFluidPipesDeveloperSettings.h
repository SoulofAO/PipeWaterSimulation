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
};
