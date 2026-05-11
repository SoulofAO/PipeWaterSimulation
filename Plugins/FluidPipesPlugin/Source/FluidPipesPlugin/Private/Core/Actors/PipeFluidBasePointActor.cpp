#include "Core/Actors/PipeFluidBasePointActor.h"
#include "EngineUtils.h"

static void ResolveAllFluidPipePointSceneNodeKeysInWorld(UWorld* World)
{
	if (!World)
	{
		return;
	}

	TArray<APipeFluidBasePointActor*> PointActors;
	for (TActorIterator<APipeFluidBasePointActor> Iterator(World); Iterator; ++Iterator)
	{
		if (*Iterator)
		{
			PointActors.Add(*Iterator);
		}
	}

	PointActors.Sort([](const APipeFluidBasePointActor& Left, const APipeFluidBasePointActor& Right)
		{
			return Left.GetFullName() < Right.GetFullName();
		});

	TSet<int32> ClaimedSceneNodeKeys;
	for (APipeFluidBasePointActor* PointActor : PointActors)
	{
		if (!PointActor)
		{
			continue;
		}

		const int32 ExistingKey = PointActor->SceneNodeKey;
		if (ExistingKey != INDEX_NONE && !ClaimedSceneNodeKeys.Contains(ExistingKey))
		{
			ClaimedSceneNodeKeys.Add(ExistingKey);
			continue;
		}

		int32 CandidateKey = 0;
		while (ClaimedSceneNodeKeys.Contains(CandidateKey))
		{
			CandidateKey += 1;
		}

		const int32 PreviousKey = ExistingKey;
		PointActor->SceneNodeKey = CandidateKey;
		ClaimedSceneNodeKeys.Add(CandidateKey);

#if WITH_EDITOR
		if (World->IsEditorWorld() && !PointActor->IsTemplate() && PreviousKey != CandidateKey)
		{
			PointActor->Modify();
		}
#endif
	}
}

void APipeFluidBasePointActor::PostActorCreated()
{
	Super::PostActorCreated();
	ResolveAllFluidPipePointSceneNodeKeysInWorld(GetWorld());
}

void APipeFluidBasePointActor::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	ResolveAllFluidPipePointSceneNodeKeysInWorld(GetWorld());
}

FFluidNetworkNodeStateZeroD APipeFluidBasePointActor::ImportFluidNetworkNodeStateZeroD() const
{
	return FFluidNetworkNodeStateZeroD();
}

FFluidSegmentStateOneD APipeFluidBasePointActor::ImportFluidSegmentStateOneDEndpoint(FFluidSegmentStateOneD Segment, bool) const
{
	return Segment;
}
