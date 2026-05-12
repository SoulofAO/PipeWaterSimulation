#pragma once

#include "CoreMinimal.h"

namespace FluidSegment1DGpuFieldIndex
{
	constexpr uint32 CellBaseIndex = 0u;
	constexpr uint32 CellCount = 1u;
	constexpr uint32 LeftBoundaryKind = 2u;
	constexpr uint32 RightBoundaryKind = 3u;
	constexpr uint32 LeftBoundaryPressure = 4u;
	constexpr uint32 RightBoundaryPressure = 5u;
	constexpr uint32 LeftBoundaryFlowUpload = 6u;
	constexpr uint32 RightBoundaryFlowUpload = 7u;
	constexpr uint32 CellLength = 8u;
	constexpr uint32 CrossSectionArea = 9u;
	constexpr uint32 SafeDensity = 10u;
	constexpr uint32 FrictionResistance = 11u;
	constexpr uint32 PressureCoefficient = 12u;
	constexpr uint32 GravitySourceTerm = 13u;
	constexpr uint32 LeftEndpointKind = 14u;
	constexpr uint32 RightEndpointKind = 15u;
	constexpr uint32 LeftConsumerReferenceGaugePressure = 16u;
	constexpr uint32 LeftConsumerVolumeFlowRatePerGaugePressureExcess = 17u;
	constexpr uint32 LeftMinimumPressureConsumerVolumeFlowRate = 18u;
	constexpr uint32 LeftMaximumPressureConsumerVolumeFlowRate = 19u;
	constexpr uint32 RightConsumerReferenceGaugePressure = 20u;
	constexpr uint32 RightConsumerVolumeFlowRatePerGaugePressureExcess = 21u;
	constexpr uint32 RightMinimumPressureConsumerVolumeFlowRate = 22u;
	constexpr uint32 RightMaximumPressureConsumerVolumeFlowRate = 23u;
	constexpr uint32 LeftSourceVolumeFlowRate = 24u;
	constexpr uint32 RightSourceVolumeFlowRate = 25u;
	constexpr uint32 UIntsPerSegment = 32u;
}

enum class EFluidSegment1DEndpointGpuKind : uint32
{
	None = 0u,
	Source = 1u,
	PressureConsumer = 2u,
	ConstantFlow = 3u,
};
