// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "Engine/ActorChannel.h"

#include "EngineClasses/SpatialNetDriver.h"
#include "Interop/Connection/SpatialWorkerConnection.h"
#include "Interop/SpatialClassInfoManager.h"
#include "Interop/SpatialStaticComponentView.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Schema/StandardLibrary.h"
#include "Schema/RPCPayload.h"
#include "SpatialCommonTypes.h"
#include "SpatialGDKSettings.h"
#include "Utils/RepDataUtils.h"
#include "Utils/SpatialStatics.h"

#include <WorkerSDK/improbable/c_worker.h>

#include "SpatialActorChannel.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSpatialActorChannel, Log, All);

struct FObjectReferences
{
	FObjectReferences() = default;
	FObjectReferences(FObjectReferences&& Other)
		: MappedRefs(MoveTemp(Other.MappedRefs))
		, UnresolvedRefs(MoveTemp(Other.UnresolvedRefs))
		, bSingleProp(Other.bSingleProp)
		, bFastArrayProp(Other.bFastArrayProp)
		, Buffer(MoveTemp(Other.Buffer))
		, NumBufferBits(Other.NumBufferBits)
		, Array(MoveTemp(Other.Array))
		, ShadowOffset(Other.ShadowOffset)
		, ParentIndex(Other.ParentIndex)
		, Property(Other.Property) {}

	// Single property constructor
	FObjectReferences(const FUnrealObjectRef& InObjectRef, bool bUnresolved, int32 InCmdIndex, int32 InParentIndex, UProperty* InProperty)
		: bSingleProp(true), bFastArrayProp(false), ShadowOffset(InCmdIndex), ParentIndex(InParentIndex), Property(InProperty)
	{
		if (bUnresolved)
		{
			UnresolvedRefs.Add(InObjectRef);
		}
		else
		{
			MappedRefs.Add(InObjectRef);
		}
	}

	// Struct (memory stream) constructor
	FObjectReferences(const TArray<uint8>& InBuffer, int32 InNumBufferBits, TSet<FUnrealObjectRef>&& InDynamicRefs, TSet<FUnrealObjectRef>&& InUnresolvedRefs, int32 InCmdIndex, int32 InParentIndex, UProperty* InProperty, bool InFastArrayProp = false)
		: MappedRefs(MoveTemp(InDynamicRefs)), UnresolvedRefs(MoveTemp(InUnresolvedRefs)), bSingleProp(false), bFastArrayProp(InFastArrayProp), Buffer(InBuffer), NumBufferBits(InNumBufferBits), ShadowOffset(InCmdIndex), ParentIndex(InParentIndex), Property(InProperty) {}

	// Array constructor
	FObjectReferences(FObjectReferencesMap* InArray, int32 InCmdIndex, int32 InParentIndex, UProperty* InProperty)
		: bSingleProp(false), bFastArrayProp(false), Array(InArray), ShadowOffset(InCmdIndex), ParentIndex(InParentIndex), Property(InProperty) {}

	TSet<FUnrealObjectRef>				MappedRefs;
	TSet<FUnrealObjectRef>				UnresolvedRefs;

	bool								bSingleProp;
	bool								bFastArrayProp;
	TArray<uint8>						Buffer;
	int32								NumBufferBits;

	TUniquePtr<FObjectReferencesMap>	Array;
	int32								ShadowOffset;
	int32								ParentIndex;
	UProperty*							Property;
};

struct FPendingSubobjectAttachment
{
	USpatialActorChannel* Channel;
	const FClassInfo* Info;
	TWeakObjectPtr<UObject> Subobject;

	TSet<Worker_ComponentId> PendingAuthorityDelegations;
};

// Utility class to manage mapped and unresolved references.
// Reproduces what is happening with FRepState::GuidReferencesMap, but with FUnrealObjectRef instead of FNetworkGUID
class FSpatialObjectRepState
{
public:

	FSpatialObjectRepState(FChannelObjectPair InThisObj) : ThisObj(InThisObj) {}

	void UpdateRefToRepStateMap(FObjectToRepStateMap& ReplicatorMap);
	bool MoveMappedObjectToUnmapped(const FUnrealObjectRef& ObjRef);
	bool HasUnresolved() const { return UnresolvedRefs.Num() == 0; }

	const FChannelObjectPair& GetChannelObjectPair() const { return ThisObj; }

	FObjectReferencesMap ReferenceMap;
	TSet< FUnrealObjectRef > ReferencedObj;
	TSet< FUnrealObjectRef > UnresolvedRefs;

private:
	bool MoveMappedObjectToUnmapped_r(const FUnrealObjectRef& ObjRef, FObjectReferencesMap& ObjectReferencesMap);
	void GatherObjectRef(TSet<FUnrealObjectRef>& OutReferenced, TSet<FUnrealObjectRef>& OutUnresolved, const FObjectReferences& References) const;

	FChannelObjectPair ThisObj;
};


UCLASS(Transient)
class SPATIALGDK_API USpatialActorChannel : public UActorChannel
{
	GENERATED_BODY()

public:
	USpatialActorChannel(const FObjectInitializer & ObjectInitializer = FObjectInitializer::Get());

	// SpatialOS Entity ID.
	FORCEINLINE Worker_EntityId GetEntityId() const
	{
		return EntityId;
	}

	FORCEINLINE void SetEntityId(Worker_EntityId InEntityId)
	{
		EntityId = InEntityId;
	}

	FORCEINLINE bool IsReadyForReplication()
	{
		// Make sure we have authority
		if (Actor->Role != ROLE_Authority)
		{
			return false;
		}

		if (EntityId != SpatialConstants::INVALID_ENTITY_ID)
		{
			// If the entity already exists, make sure we have spatial authority before we replicate with Offloading, because we pretend to have local authority
			if (USpatialStatics::IsSpatialOffloadingEnabled() && !bCreatingNewEntity && !NetDriver->StaticComponentView->HasAuthority(EntityId, SpatialConstants::POSITION_COMPONENT_ID))
			{
				return false;
			}

			return true;
		}

		// This could happen if we've run out of entity ids at the time we called SetChannelActor.
		// If that is the case, keep trying to allocate an entity ID until we succeed.
		return TryResolveActor();
	}

	// Called on the client when receiving an update.
	FORCEINLINE bool IsClientAutonomousProxy()
	{
		if (NetDriver->GetNetMode() != NM_Client)
		{
			return false;
		}

		return NetDriver->StaticComponentView->HasAuthority(EntityId, SpatialConstants::GetClientAuthorityComponent(GetDefault<USpatialGDKSettings>()->UseRPCRingBuffer()));
	}

	inline void SetClientAuthority(const bool IsAuth)
	{
		bIsAuthClient = IsAuth;
	}


	// Indicates whether this client worker has "ownership" (authority over Client endpoint) over the entity corresponding to this channel.
	inline bool IsAuthoritativeClient() const
	{
		if (GetDefault<USpatialGDKSettings>()->bEnableResultTypes)
		{
			return bIsAuthClient;
		}

		// If we aren't using result types, we have to actually look at the ACL to see if we should be authoritative or not to guess if we are going to receive authority
		// in order to send dynamic interest overrides correctly for this client. If we don't do this there's a good chance we will see that there is no server RPC endpoint
		// on this entity when we try to send any RPCs immediately after checking out the entity, which can lead to inconsistent state.
		const TArray<FString>& WorkerAttributes = NetDriver->Connection->GetWorkerAttributes();
		if (const SpatialGDK::EntityAcl* EntityACL = NetDriver->StaticComponentView->GetComponentData<SpatialGDK::EntityAcl>(EntityId))
		{
			if (const WorkerRequirementSet* WorkerRequirementsSet = EntityACL->ComponentWriteAcl.Find(SpatialConstants::GetClientAuthorityComponent(GetDefault<USpatialGDKSettings>()->UseRPCRingBuffer()))) {
				for (const WorkerAttributeSet& AttributeSet : *WorkerRequirementsSet)
				{
					for (const FString& Attribute : AttributeSet)
					{
						if (WorkerAttributes.Contains(Attribute))
						{
							return true;
						}
					}
				}
			}
		}
	
		return false;
	}

	// Sets the server and client authorities for this SpatialActorChannel based on the StaticComponentView
	inline void RefreshAuthority()
	{
		if (NetDriver->IsServer())
		{
			SetServerAuthority(NetDriver->StaticComponentView->HasAuthority(EntityId, SpatialConstants::POSITION_COMPONENT_ID));
		}
		else
		{
			SetClientAuthority(NetDriver->StaticComponentView->HasAuthority(EntityId, SpatialConstants::GetClientAuthorityComponent(GetDefault<USpatialGDKSettings>()->UseRPCRingBuffer())));
		}
	}

	inline void SetServerAuthority(const bool IsAuth)
	{
		bIsAuthServer = IsAuth;
	}

	inline bool IsAuthoritativeServer() const
	{
		return bIsAuthServer;
	}

	FORCEINLINE FRepLayout& GetObjectRepLayout(UObject* Object)
	{
		check(ObjectHasReplicator(Object));
		return *FindOrCreateReplicator(Object)->RepLayout;
	}

	FORCEINLINE FRepStateStaticBuffer& GetObjectStaticBuffer(UObject* Object)
	{
		check(ObjectHasReplicator(Object));
#if ENGINE_MINOR_VERSION <= 22
		return FindOrCreateReplicator(Object)->RepState->StaticBuffer;
#else
		return FindOrCreateReplicator(Object)->RepState->GetReceivingRepState()->StaticBuffer;
#endif
	}

	// Begin UChannel interface
	virtual void Init(UNetConnection * InConnection, int32 ChannelIndex, EChannelCreateFlags CreateFlag) override;
	virtual int64 Close(EChannelCloseReason Reason) override;
	// End UChannel interface

	// Begin UActorChannel interface
	virtual int64 ReplicateActor() override;
#if ENGINE_MINOR_VERSION <= 22
	virtual void SetChannelActor(AActor* InActor) override;
#else
	virtual void SetChannelActor(AActor* InActor, ESetChannelActorFlags Flags) override;
#endif
	virtual bool ReplicateSubobject(UObject* Obj, FOutBunch& Bunch, const FReplicationFlags& RepFlags) override;
	virtual bool ReadyForDormancy(bool suppressLogs = false) override;
	// End UActorChannel interface

	bool TryResolveActor();

	bool ReplicateSubobject(UObject* Obj, const FReplicationFlags& RepFlags);

	TMap<UObject*, const FClassInfo*> GetHandoverSubobjects();

	FRepChangeState CreateInitialRepChangeState(TWeakObjectPtr<UObject> Object);
	FHandoverChangeState CreateInitialHandoverChangeState(const FClassInfo& ClassInfo);

	// For an object that is replicated by this channel (i.e. this channel's actor or its component), find out whether a given handle is an array.
	bool IsDynamicArrayHandle(UObject* Object, uint16 Handle);

	FObjectReplicator* PreReceiveSpatialUpdate(UObject* TargetObject);
	void PostReceiveSpatialUpdate(UObject* TargetObject, const TArray<UProperty*>& RepNotifies);

	void OnCreateEntityResponse(const Worker_CreateEntityResponseOp& Op);

	void RemoveRepNotifiesWithUnresolvedObjs(TArray<UProperty*>& RepNotifies, const FRepLayout& RepLayout, const FObjectReferencesMap& RefMap, UObject* Object);
	
	void UpdateShadowData();
	void UpdateSpatialPositionWithFrequencyCheck();
	void UpdateSpatialPosition();

	void ServerProcessOwnershipChange();
	void ClientProcessOwnershipChange(bool bNewNetOwned);

	FORCEINLINE void MarkInterestDirty() { bInterestDirty = true; }
	FORCEINLINE bool GetInterestDirty() const { return bInterestDirty; }

	bool IsListening() const;

	// Call when a subobject is deleted to unmap its references and cleanup its cached informations.
	void OnSubobjectDeleted(const FUnrealObjectRef& ObjectRef, UObject* Object);

	static void ResetShadowData(FRepLayout& RepLayout, FRepStateStaticBuffer& StaticBuffer, UObject* TargetObject);

protected:
	// Begin UChannel interface
	virtual bool CleanUp(const bool bForDestroy, EChannelCloseReason CloseReason) override;
	// End UChannel interface

private:
	void DynamicallyAttachSubobject(UObject* Object);

	void DeleteEntityIfAuthoritative();

	void SendPositionUpdate(AActor* InActor, Worker_EntityId InEntityId, const FVector& NewPosition);

	void InitializeHandoverShadowData(TArray<uint8>& ShadowData, UObject* Object);
	FHandoverChangeState GetHandoverChangeList(TArray<uint8>& ShadowData, UObject* Object);
	
public:
	// If this actor channel is responsible for creating a new entity, this will be set to true once the entity creation request is issued.
	bool bCreatedEntity;

	// If this actor channel is responsible for creating a new entity, this will be set to true during initial replication.
	bool bCreatingNewEntity;

	TSet<TWeakObjectPtr<UObject>> PendingDynamicSubobjects;

	TMap<TWeakObjectPtr<UObject>, FSpatialObjectRepState> ObjectReferenceMap;

private:
	Worker_EntityId EntityId;
	bool bInterestDirty;

	bool bIsAuthServer;
	bool bIsAuthClient;

	// Used on the client to track gaining/losing ownership.
	bool bNetOwned;

	// Used on the server
	// Tracks the client worker ID corresponding to the owning connection.
	// If no owning client connection exists, this will be an empty string.
	FString SavedConnectionOwningWorkerId;

	// Used on the server
	// Tracks the interest bucket component ID for the relevant Actor.
	Worker_ComponentId SavedInterestBucketComponentID;

	UPROPERTY(transient)
	USpatialNetDriver* NetDriver;

	UPROPERTY(transient)
	class USpatialSender* Sender;

	UPROPERTY(transient)
	class USpatialReceiver* Receiver;

	FVector LastPositionSinceUpdate;
	float TimeWhenPositionLastUpdated;

	uint8 FramesTillDormancyAllowed = 0;

	// This is incremented in ReplicateActor. It represents how many bytes are sent per call to ReplicateActor.
	// ReplicationBytesWritten is reset back to 0 at the start of ReplicateActor.
	uint32 ReplicationBytesWritten = 0;

	// Shadow data for Handover properties.
	// For each object with handover properties, we store a blob of memory which contains
	// the state of those properties at the last time we sent them, and is used to detect
	// when those properties change.
	TArray<uint8>* ActorHandoverShadowData;
	TMap<TWeakObjectPtr<UObject>, TSharedRef<TArray<uint8>>> HandoverShadowDataMap;
};
