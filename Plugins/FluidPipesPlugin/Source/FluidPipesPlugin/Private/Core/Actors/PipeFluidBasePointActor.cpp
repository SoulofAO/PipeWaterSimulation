#include "Core/Actors/PipeFluidBasePointActor.h"

FFluidNetworkNodeStateZeroD APipeFluidBasePointActor::ImportFluidNetworkNodeStateZeroD() const
{
	return FFluidNetworkNodeStateZeroD();
}

FFluidSegmentStateOneD APipeFluidBasePointActor::ImportFluidSegmentStateOneDEndpoint(FFluidSegmentStateOneD Segment, bool) const
{
	return Segment;
}
