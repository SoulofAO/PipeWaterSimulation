#include "Core/Actors/PipeFluidBasePointActor.h"
#include "Core/Actors/PipeFluidPipeActor.h"
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

#if WITH_EDITOR
void APipeFluidBasePointActor::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	UWorld* World = GetWorld();
	if (!World || World->IsPreviewWorld())
	{
		return;
	}

	for (TActorIterator<APipeFluidPipeActor> Iterator(World); Iterator; ++Iterator)
	{
		APipeFluidPipeActor* PipeActor = *Iterator;
		if (!PipeActor || PipeActor->IsTemplate())
		{
			continue;
		}

		if (PipeActor->PipeEndpointFirst == this || PipeActor->PipeEndpointSecond == this)
		{
			PipeActor->EditorRefreshFluidPipeAttachmentToAttachedEndpoints();
		}
	}
}
#endif

FFluidNetworkNodeStateZeroD APipeFluidBasePointActor::ImportFluidNetworkNodeStateZeroD() const
{
	return FFluidNetworkNodeStateZeroD();
}

FFluidSegmentStateOneD APipeFluidBasePointActor::ImportFluidSegmentStateOneDEndpoint(FFluidSegmentStateOneD Segment, bool) const
{
	return Segment;
}

float APipeFluidBasePointActor::EvaluateRuntimeZeroDimensionExternalVolumeFlowContribution(float) const
{
	return 0.0f;
}

float APipeFluidBasePointActor::ComputeRuntimeSignedVolumeFlowRateForOneDimensionPipeBoundary(bool, float) const
{
	return 0.0f;
}
