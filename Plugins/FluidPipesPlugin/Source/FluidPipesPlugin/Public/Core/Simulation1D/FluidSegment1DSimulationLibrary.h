#pragma once

#include "Data/FluidData.h"

struct FFluidSegment1DSimulationLibrary
{
	static float PressureSampleNearBoundaryForJunctionCoupling(const TArray<FFluidSegmentCellStateOneD>& CellStates, bool bLeftEndpoint)
	{
		const int32 CellCount = CellStates.Num();
		if (CellCount < 2)
		{
			return 0.0f;
		}
		if (bLeftEndpoint)
		{
			if (CellCount >= 3)
			{
				return 0.5f * (CellStates[1].Pressure + CellStates[2].Pressure);
			}
			return CellStates[1].Pressure;
		}
		if (CellCount >= 3)
		{
			return 0.5f * (CellStates[CellCount - 2].Pressure + CellStates[CellCount - 3].Pressure);
		}
		return CellStates[CellCount - 2].Pressure;
	}
};
