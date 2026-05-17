#pragma once

#include "CoreMinimal.h"
#include "FluidPipesSimulationSettingsTypes.generated.h"

UENUM(BlueprintType)
enum class EFluidLevelPipeImportTarget : uint8
{
	Disabled,
	ZeroDNetwork,
	OneDSegments,
	Both
};

UENUM(BlueprintType)
enum class EFluidSegmentSimulationOneDBackend : uint8
{
	CpuGameThread,
	CpuBackgroundThread,
	GpuComputeShader UMETA(DisplayName = "Gpu Compute Shader"),
	GpuComputeShaderSynchronous UMETA(DisplayName = "Gpu Compute Shader Synchronous")
};

inline bool FluidSegmentSimulationOneDUsesGpuComputeBackend(EFluidSegmentSimulationOneDBackend Backend)
{
	return Backend == EFluidSegmentSimulationOneDBackend::GpuComputeShader
		|| Backend == EFluidSegmentSimulationOneDBackend::GpuComputeShaderSynchronous;
}

inline bool FluidSegmentSimulationOneDRequiresGpuStepCompletionWait(EFluidSegmentSimulationOneDBackend Backend)
{
	return Backend == EFluidSegmentSimulationOneDBackend::GpuComputeShaderSynchronous;
}
