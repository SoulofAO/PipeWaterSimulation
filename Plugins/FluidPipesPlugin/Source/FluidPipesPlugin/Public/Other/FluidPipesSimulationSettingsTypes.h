#pragma once

#include "CoreMinimal.h"
#include "FluidPipesSimulationSettingsTypes.generated.h"

UENUM(BlueprintType)
enum class EFluidLevelPipeImportTarget : uint8
{
	Disabled,
	ZeroDNetwork,
	OneDSegments,
	Both,
	HybridNetwork
};

UENUM(BlueprintType)
enum class EFluidSegmentSimulationOneDBackend : uint8
{
	CpuGameThread,
	CpuBackgroundThread,
	GpuComputeShader,
	GpuComputeShaderSynchronous
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

UENUM(BlueprintType)
enum class EFluidNetworkSimulationZeroDBackend : uint8
{
	CpuGameThread,
	CpuBackgroundThread,
	GpuComputeShader,
	GpuComputeShaderSynchronous
};

inline bool FluidNetworkSimulationZeroDUsesGpuComputeBackend(EFluidNetworkSimulationZeroDBackend Backend)
{
	return Backend == EFluidNetworkSimulationZeroDBackend::GpuComputeShader
		|| Backend == EFluidNetworkSimulationZeroDBackend::GpuComputeShaderSynchronous;
}

inline bool FluidNetworkSimulationZeroDRequiresGpuStepCompletionWait(EFluidNetworkSimulationZeroDBackend Backend)
{
	return Backend == EFluidNetworkSimulationZeroDBackend::GpuComputeShaderSynchronous;
}
