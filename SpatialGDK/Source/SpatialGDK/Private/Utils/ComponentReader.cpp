// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "Utils/ComponentReader.h"

#include "Engine/BlueprintGeneratedClass.h"
#include "Net/DataReplication.h"
#include "Net/RepLayout.h"
#include "UObject/TextProperty.h"

#include "EngineClasses/SpatialFastArrayNetSerialize.h"
#include "EngineClasses/SpatialNetBitReader.h"
#include "Interop/SpatialConditionMapFilter.h"
#include "SpatialConstants.h"
#include "Utils/SchemaUtils.h"
#include "Utils/RepLayoutUtils.h"

DEFINE_LOG_CATEGORY(LogSpatialComponentReader);

DECLARE_CYCLE_STAT(TEXT("Reader ApplyPropertyUpdates"), STAT_ReaderApplyPropertyUpdates, STATGROUP_SpatialNet);
DECLARE_CYCLE_STAT(TEXT("Reader ApplyHandoverPropertyUpdates"), STAT_ReaderApplyHandoverPropertyUpdates, STATGROUP_SpatialNet);
DECLARE_CYCLE_STAT(TEXT("Reader ApplyFastArrayUpdate"), STAT_ReaderApplyFastArrayUpdate, STATGROUP_SpatialNet);
DECLARE_CYCLE_STAT(TEXT("Reader ApplyProperty"), STAT_ReaderApplyProperty, STATGROUP_SpatialNet);
DECLARE_CYCLE_STAT(TEXT("Reader ApplyArray"), STAT_ReaderApplyArray, STATGROUP_SpatialNet);

namespace
{
	bool FORCEINLINE ObjectRefSetsAreSame(const TSet< FUnrealObjectRef >& A, const TSet< FUnrealObjectRef >& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}

		for (const FUnrealObjectRef& CompareRef : A)
		{
			if (!B.Contains(CompareRef))
			{
				return false;
			}
		}

		return true;
	}

	bool ReferencesChanged(FObjectReferencesMap& InObjectReferencesMap, int32 Offset, bool bHasReferences, const TSet<FUnrealObjectRef>& NewDynamicRefs, const TSet<FUnrealObjectRef> NewUnresolvedRefs)
	{
		FObjectReferences* CurEntry = InObjectReferencesMap.Find(Offset);

		if (bHasReferences ^ (CurEntry != nullptr))
		{
			return true;
		}
		if (CurEntry && bHasReferences)
		{
			return !ObjectRefSetsAreSame(NewDynamicRefs, CurEntry->MappedRefs) || !ObjectRefSetsAreSame(NewUnresolvedRefs, CurEntry->UnresolvedRefs);
		}
		return false;
	}

	bool ReferencesChanged(FObjectReferencesMap& InObjectReferencesMap, int32 Offset, bool bHasReferences, const FUnrealObjectRef& ObjectRef, bool bUnresolved)
	{
		FObjectReferences* CurEntry = InObjectReferencesMap.Find(Offset);

		if (bHasReferences ^ (CurEntry != nullptr))
		{
			return true;
		}
		if (CurEntry && bHasReferences)
		{
			if (!bUnresolved)
			{
				return CurEntry->MappedRefs.Num() != 1 || CurEntry->UnresolvedRefs.Num() != 0 || *CurEntry->MappedRefs.begin() != ObjectRef;
			}
			else
			{
				return CurEntry->MappedRefs.Num() != 0 || CurEntry->UnresolvedRefs.Num() != 1 || *CurEntry->UnresolvedRefs.begin() != ObjectRef;
			}

		}
		return false;
	}
}

namespace SpatialGDK
{

ComponentReader::ComponentReader(USpatialNetDriver* InNetDriver, FObjectReferencesMap& InObjectReferencesMap/*, TSet<FUnrealObjectRef>& InUnresolvedRefs*/)
	: PackageMap(InNetDriver->PackageMap)
	, NetDriver(InNetDriver)
	, ClassInfoManager(InNetDriver->ClassInfoManager)
	, RootObjectReferencesMap(InObjectReferencesMap)
{
}

void ComponentReader::ApplyComponentData(const Worker_ComponentData& ComponentData, UObject& Object, USpatialActorChannel& Channel, bool bIsHandover, bool& bOutReferencesChanged)
{
	if (Object.IsPendingKill())
	{
		return;
	}

	Schema_Object* ComponentObject = Schema_GetComponentDataFields(ComponentData.schema_type);

	TArray<uint32> UpdatedIds;
	UpdatedIds.SetNumUninitialized(Schema_GetUniqueFieldIdCount(ComponentObject));
	Schema_GetUniqueFieldIds(ComponentObject, UpdatedIds.GetData());

	if (bIsHandover)
	{
		ApplyHandoverSchemaObject(ComponentObject, Object, Channel, true, UpdatedIds, ComponentData.component_id, bOutReferencesChanged);
	}
	else
	{
		ApplySchemaObject(ComponentObject, Object, Channel, true, UpdatedIds, ComponentData.component_id, bOutReferencesChanged);
	}
}

void ComponentReader::ApplyComponentUpdate(const Worker_ComponentUpdate& ComponentUpdate, UObject& Object, USpatialActorChannel& Channel, bool bIsHandover, bool& bOutReferencesChanged)
{
	if (Object.IsPendingKill())
	{
		return;
	}

	Schema_Object* ComponentObject = Schema_GetComponentUpdateFields(ComponentUpdate.schema_type);

	// Retrieve all the fields that have been updated in this component update
	TArray<uint32> UpdatedIds;
	UpdatedIds.SetNumUninitialized(Schema_GetUniqueFieldIdCount(ComponentObject));
	Schema_GetUniqueFieldIds(ComponentObject, UpdatedIds.GetData());

	// Retrieve all the fields that have been cleared (eg. list with no entries)
	TArray<Schema_FieldId> ClearedIds;
	ClearedIds.SetNumUninitialized(Schema_GetComponentUpdateClearedFieldCount(ComponentUpdate.schema_type));
	Schema_GetComponentUpdateClearedFieldList(ComponentUpdate.schema_type, ClearedIds.GetData());

	// Merge cleared fields into updated fields to ensure they will be processed (Schema_FieldId == uint32)
	UpdatedIds.Append(ClearedIds);

	if (UpdatedIds.Num() > 0)
	{
		if (bIsHandover)
		{
			ApplyHandoverSchemaObject(ComponentObject, Object, Channel, false, UpdatedIds, ComponentUpdate.component_id, bOutReferencesChanged);
		}
		else
		{
			ApplySchemaObject(ComponentObject, Object, Channel, false, UpdatedIds, ComponentUpdate.component_id, bOutReferencesChanged);
		}
	}
}

void ComponentReader::ApplySchemaObject(Schema_Object* ComponentObject, UObject& Object, USpatialActorChannel& Channel, bool bIsInitialData, const TArray<Schema_FieldId>& UpdatedIds, Worker_ComponentId ComponentId, bool& bOutReferencesChanged)
{
	FObjectReplicator* Replicator = Channel.PreReceiveSpatialUpdate(&Object);
	if (Replicator == nullptr)
	{
		// Can't apply this schema object. Error printed from PreReceiveSpatialUpdate.
		return;
	}

	TUniquePtr<FRepState>& RepState = Replicator->RepState;
	TArray<FRepLayoutCmd>& Cmds = Replicator->RepLayout->Cmds;
	TArray<FHandleToCmdIndex>& BaseHandleToCmdIndex = Replicator->RepLayout->BaseHandleToCmdIndex;
	TArray<FRepParentCmd>& Parents = Replicator->RepLayout->Parents;

	bool bIsAuthServer = Channel.IsAuthoritativeServer();
	bool bAutonomousProxy = Channel.IsClientAutonomousProxy();
	bool bIsClient = NetDriver->GetNetMode() == NM_Client;

	FSpatialConditionMapFilter ConditionMap(&Channel, bIsClient);

	TArray<UProperty*> RepNotifies;

	{
		// Scoped to exclude OnRep callbacks which are already tracked per OnRep function
		SCOPE_CYCLE_COUNTER(STAT_ReaderApplyPropertyUpdates);

		for (uint32 FieldId : UpdatedIds)
		{
			// FieldId is the same as rep handle
			if (FieldId == 0 || (int)FieldId - 1 >= BaseHandleToCmdIndex.Num())
			{
				UE_LOG(LogSpatialComponentReader, Error, TEXT("ApplySchemaObject: Encountered an invalid field Id while applying schema. Object: %s, Field: %d, Entity: %lld, Component: %d"), *Object.GetPathName(), FieldId, Channel.GetEntityId(), ComponentId);
				continue;
			}

			int32 CmdIndex = BaseHandleToCmdIndex[FieldId - 1].CmdIndex;
			const FRepLayoutCmd& Cmd = Cmds[CmdIndex];
			const FRepParentCmd& Parent = Parents[Cmd.ParentIndex];
			int32 ShadowOffset = Cmd.ShadowOffset;

			if (NetDriver->IsServer() || ConditionMap.IsRelevant(Parent.Condition))
			{
				// This swaps Role/RemoteRole as we write it
				const FRepLayoutCmd& SwappedCmd = (!bIsAuthServer && Parent.RoleSwapIndex != -1) ? Cmds[Parents[Parent.RoleSwapIndex].CmdStart] : Cmd;

				uint8* Data = (uint8*)&Object + SwappedCmd.Offset;

				// If the property has RepNotifies, update with local data and possibly initialize the shadow data
				if (Parent.Property->HasAnyPropertyFlags(CPF_RepNotify))
				{
#if ENGINE_MINOR_VERSION <= 22
					FRepStateStaticBuffer& ShadowData = RepState->StaticBuffer;
#else
					FRepStateStaticBuffer& ShadowData = RepState->GetReceivingRepState()->StaticBuffer;
#endif
					if (ShadowData.Num() == 0)
					{
						Channel.ResetShadowData(*Replicator->RepLayout.Get(), ShadowData, &Object);
					}
					else
					{
						Cmd.Property->CopySingleValue(ShadowData.GetData() + SwappedCmd.ShadowOffset, Data);
					}
				}

				if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
				{
					UArrayProperty* ArrayProperty = Cast<UArrayProperty>(Cmd.Property);
					if (ArrayProperty == nullptr)
					{
						UE_LOG(LogSpatialComponentReader, Error, TEXT("Failed to apply Schema Object %s. One of it's properties is null"), *Object.GetName());
						continue;
					}

					// Check if this is a FastArraySerializer array and if so, call our custom delta serialization
					if (UScriptStruct* NetDeltaStruct = GetFastArraySerializerProperty(ArrayProperty))
					{
						SCOPE_CYCLE_COUNTER(STAT_ReaderApplyFastArrayUpdate);

						TArray<uint8> ValueData = GetBytesFromSchema(ComponentObject, FieldId);
						int64 CountBits = ValueData.Num() * 8;
						TSet<FUnrealObjectRef> NewMappedRefs;
						TSet<FUnrealObjectRef> NewUnresolvedRefs;
						FSpatialNetBitReader ValueDataReader(PackageMap, ValueData.GetData(), CountBits, NewMappedRefs, NewUnresolvedRefs);

						if (ValueData.Num() > 0)
						{
							FSpatialNetDeltaSerializeInfo::DeltaSerializeRead(NetDriver, ValueDataReader, &Object, Parent.ArrayIndex, Parent.Property, NetDeltaStruct);
						}

						FObjectReferences* CurEntry = RootObjectReferencesMap.Find(SwappedCmd.Offset);
						const bool bHasReferences = NewUnresolvedRefs.Num() > 0 || NewMappedRefs.Num() > 0;

						if (ReferencesChanged(RootObjectReferencesMap, SwappedCmd.Offset, bHasReferences, NewMappedRefs, NewUnresolvedRefs))
						{
							if (bHasReferences)
							{
								RootObjectReferencesMap.Add(SwappedCmd.Offset, FObjectReferences(ValueData, CountBits, MoveTemp(NewMappedRefs), MoveTemp(NewUnresolvedRefs), ShadowOffset, Cmd.ParentIndex, ArrayProperty, /* bFastArrayProp */ true));
							}
							else
							{
								RootObjectReferencesMap.Remove(SwappedCmd.Offset);
							}
							bOutReferencesChanged = true;
						}
					}
					else
					{
						ApplyArray(ComponentObject, FieldId, RootObjectReferencesMap, ArrayProperty, Data, SwappedCmd.Offset, ShadowOffset, Cmd.ParentIndex, bOutReferencesChanged);
					}
				}
				else
				{
					ApplyProperty(ComponentObject, FieldId, RootObjectReferencesMap, 0, Cmd.Property, Data, SwappedCmd.Offset, ShadowOffset, Cmd.ParentIndex, bOutReferencesChanged);
				}

				if (Cmd.Property->GetFName() == NAME_RemoteRole)
				{
					// Downgrade role from AutonomousProxy to SimulatedProxy if we aren't authoritative over
					// the client RPCs component.
					UByteProperty* ByteProperty = Cast<UByteProperty>(Cmd.Property);
					if (!bIsAuthServer && !bAutonomousProxy && ByteProperty->GetPropertyValue(Data) == ROLE_AutonomousProxy)
					{
						ByteProperty->SetPropertyValue(Data, ROLE_SimulatedProxy);
					}
				}

				// Parent.Property is the "root" replicated property, e.g. if a struct property was flattened
				if (Parent.Property->HasAnyPropertyFlags(CPF_RepNotify))
				{
	#if ENGINE_MINOR_VERSION <= 22
					bool bIsIdentical = Cmd.Property->Identical(RepState->StaticBuffer.GetData() + SwappedCmd.ShadowOffset, Data);
	#else
					bool bIsIdentical = Cmd.Property->Identical(RepState->GetReceivingRepState()->StaticBuffer.GetData() + SwappedCmd.ShadowOffset, Data);
	#endif

					// Only call RepNotify for REPNOTIFY_Always if we are not applying initial data.
					if (bIsInitialData)
					{
						if (!bIsIdentical)
						{
							RepNotifies.AddUnique(Parent.Property);
						}
					}
					else
					{
						if (Parent.RepNotifyCondition == REPNOTIFY_Always || !bIsIdentical)
						{
							RepNotifies.AddUnique(Parent.Property);
						}
					}
				}
			}
		}
	}

	Channel.RemoveRepNotifiesWithUnresolvedObjs(RepNotifies, *Replicator->RepLayout, RootObjectReferencesMap, &Object);

	Channel.PostReceiveSpatialUpdate(&Object, RepNotifies);
}

void ComponentReader::ApplyHandoverSchemaObject(Schema_Object* ComponentObject, UObject& Object, USpatialActorChannel& Channel, bool bIsInitialData, const TArray<Schema_FieldId>& UpdatedIds, Worker_ComponentId ComponentId, bool& bOutReferencesChanged)
{
	SCOPE_CYCLE_COUNTER(STAT_ReaderApplyHandoverPropertyUpdates);

	FObjectReplicator* Replicator = Channel.PreReceiveSpatialUpdate(&Object);
	if (Replicator == nullptr)
	{
		// Can't apply this schema object. Error printed from PreReceiveSpatialUpdate.
		return;
	}

	const FClassInfo& ClassInfo = ClassInfoManager->GetOrCreateClassInfoByClass(Object.GetClass());

	for (uint32 FieldId : UpdatedIds)
	{
		// FieldId is the same as handover handle
		if (FieldId == 0 || (int)FieldId - 1 >= ClassInfo.HandoverProperties.Num())
		{
			UE_LOG(LogSpatialComponentReader, Error, TEXT("ApplyHandoverSchemaObject: Encountered an invalid field Id while applying schema. Object: %s, Field: %d, Entity: %lld, Component: %d"), *Object.GetPathName(), FieldId, Channel.GetEntityId(), ComponentId);
			continue;
		}
		const FHandoverPropertyInfo& PropertyInfo = ClassInfo.HandoverProperties[FieldId - 1];

		uint8* Data = (uint8*)&Object + PropertyInfo.Offset;

		if (UArrayProperty* ArrayProperty = Cast<UArrayProperty>(PropertyInfo.Property))
		{
			ApplyArray(ComponentObject, FieldId, RootObjectReferencesMap, ArrayProperty, Data, PropertyInfo.Offset, -1, -1, bOutReferencesChanged);
		}
		else
		{
			ApplyProperty(ComponentObject, FieldId, RootObjectReferencesMap, 0, PropertyInfo.Property, Data, PropertyInfo.Offset, -1, -1, bOutReferencesChanged);
		}
	}

	Channel.PostReceiveSpatialUpdate(&Object, TArray<UProperty*>());
}

void ComponentReader::ApplyProperty(Schema_Object* Object, Schema_FieldId FieldId, FObjectReferencesMap& InObjectReferencesMap, uint32 Index, UProperty* Property, uint8* Data, int32 Offset, int32 ShadowOffset, int32 ParentIndex, bool& bOutReferencesChanged)
{
	SCOPE_CYCLE_COUNTER(STAT_ReaderApplyProperty);

	if (UStructProperty* StructProperty = Cast<UStructProperty>(Property))
	{
		TArray<uint8> ValueData = IndexBytesFromSchema(Object, FieldId, Index);
		// A bit hacky, we should probably include the number of bits with the data instead.
		int64 CountBits = ValueData.Num() * 8;
		TSet<FUnrealObjectRef> NewDynamicRefs;
		TSet<FUnrealObjectRef> NewUnresolvedRefs;
		FSpatialNetBitReader ValueDataReader(PackageMap, ValueData.GetData(), CountBits, NewDynamicRefs, NewUnresolvedRefs);
		bool bHasUnmapped = false;

		ReadStructProperty(ValueDataReader, StructProperty, NetDriver, Data, bHasUnmapped);
		const bool bHasReferences = NewDynamicRefs.Num() > 0 || NewUnresolvedRefs.Num() > 0;

		if (ReferencesChanged(InObjectReferencesMap, Offset, bHasReferences, NewDynamicRefs, NewUnresolvedRefs))
		{
			if (bHasReferences)
			{
				InObjectReferencesMap.Add(Offset, FObjectReferences(ValueData, CountBits, MoveTemp(NewDynamicRefs), MoveTemp(NewUnresolvedRefs), ShadowOffset, ParentIndex, Property));
			}
			else
			{
				InObjectReferencesMap.Remove(Offset);
			}
			
			bOutReferencesChanged = true;
		}
	}
	else if (UBoolProperty* BoolProperty = Cast<UBoolProperty>(Property))
	{
		BoolProperty->SetPropertyValue(Data, Schema_IndexBool(Object, FieldId, Index) != 0);
	}
	else if (UFloatProperty* FloatProperty = Cast<UFloatProperty>(Property))
	{
		FloatProperty->SetPropertyValue(Data, Schema_IndexFloat(Object, FieldId, Index));
	}
	else if (UDoubleProperty* DoubleProperty = Cast<UDoubleProperty>(Property))
	{
		DoubleProperty->SetPropertyValue(Data, Schema_IndexDouble(Object, FieldId, Index));
	}
	else if (UInt8Property* Int8Property = Cast<UInt8Property>(Property))
	{
		Int8Property->SetPropertyValue(Data, (int8)Schema_IndexInt32(Object, FieldId, Index));
	}
	else if (UInt16Property* Int16Property = Cast<UInt16Property>(Property))
	{
		Int16Property->SetPropertyValue(Data, (int16)Schema_IndexInt32(Object, FieldId, Index));
	}
	else if (UIntProperty* IntProperty = Cast<UIntProperty>(Property))
	{
		IntProperty->SetPropertyValue(Data, Schema_IndexInt32(Object, FieldId, Index));
	}
	else if (UInt64Property* Int64Property = Cast<UInt64Property>(Property))
	{
		Int64Property->SetPropertyValue(Data, Schema_IndexInt64(Object, FieldId, Index));
	}
	else if (UByteProperty* ByteProperty = Cast<UByteProperty>(Property))
	{
		ByteProperty->SetPropertyValue(Data, (uint8)Schema_IndexUint32(Object, FieldId, Index));
	}
	else if (UUInt16Property* UInt16Property = Cast<UUInt16Property>(Property))
	{
		UInt16Property->SetPropertyValue(Data, (uint16)Schema_IndexUint32(Object, FieldId, Index));
	}
	else if (UUInt32Property* UInt32Property = Cast<UUInt32Property>(Property))
	{
		UInt32Property->SetPropertyValue(Data, Schema_IndexUint32(Object, FieldId, Index));
	}
	else if (UUInt64Property* UInt64Property = Cast<UUInt64Property>(Property))
	{
		UInt64Property->SetPropertyValue(Data, Schema_IndexUint64(Object, FieldId, Index));
	}
	else if (UObjectPropertyBase* ObjectProperty = Cast<UObjectPropertyBase>(Property))
	{
		FUnrealObjectRef ObjectRef = IndexObjectRefFromSchema(Object, FieldId, Index);
		check(ObjectRef != FUnrealObjectRef::UNRESOLVED_OBJECT_REF);

		if (Cast<USoftObjectProperty>(Property))
		{
			FSoftObjectPtr* ObjectPtr = reinterpret_cast<FSoftObjectPtr*>(Data);
			*ObjectPtr = FUnrealObjectRef::ToSoftObjectPath(ObjectRef);
		}
		else
		{
			bool bUnresolved = false;
			UObject* ObjectValue = FUnrealObjectRef::ToObjectPtr(ObjectRef, PackageMap, bUnresolved);

			const bool bHasReferences = bUnresolved || (ObjectValue && !ObjectValue->IsFullNameStableForNetworking());

			if (ReferencesChanged(InObjectReferencesMap, Offset, bHasReferences, ObjectRef, bUnresolved))
			{
				if (bHasReferences)
				{
					InObjectReferencesMap.Add(Offset, FObjectReferences(ObjectRef, bUnresolved, ShadowOffset, ParentIndex, Property));
				}
				else
				{
					InObjectReferencesMap.Remove(Offset);
				}
				bOutReferencesChanged = true;
			}
			if(!bUnresolved)
			{
				ObjectProperty->SetObjectPropertyValue(Data, ObjectValue);
				if (ObjectValue != nullptr)
				{
					checkf(ObjectValue->IsA(ObjectProperty->PropertyClass), TEXT("Object ref %s maps to object %s with the wrong class."), *ObjectRef.ToString(), *ObjectValue->GetFullName());
				}
			}
		}
	}
	else if (UNameProperty* NameProperty = Cast<UNameProperty>(Property))
	{
		NameProperty->SetPropertyValue(Data, FName(*IndexStringFromSchema(Object, FieldId, Index)));
	}
	else if (UStrProperty* StrProperty = Cast<UStrProperty>(Property))
	{
		StrProperty->SetPropertyValue(Data, IndexStringFromSchema(Object, FieldId, Index));
	}
	else if (UTextProperty* TextProperty = Cast<UTextProperty>(Property))
	{
		TextProperty->SetPropertyValue(Data, FText::FromString(IndexStringFromSchema(Object, FieldId, Index)));
	}
	else if (UEnumProperty* EnumProperty = Cast<UEnumProperty>(Property))
	{
		if (EnumProperty->ElementSize < 4)
		{
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(Data, (uint64)Schema_IndexUint32(Object, FieldId, Index));
		}
		else
		{
			ApplyProperty(Object, FieldId, InObjectReferencesMap, Index, EnumProperty->GetUnderlyingProperty(), Data, Offset, ShadowOffset, ParentIndex, bOutReferencesChanged);
		}
	}
	else
	{
		checkf(false, TEXT("Tried to read unknown property in field %d"), FieldId);
	}
}

void ComponentReader::ApplyArray(Schema_Object* Object, Schema_FieldId FieldId, FObjectReferencesMap& InObjectReferencesMap, UArrayProperty* Property, uint8* Data, int32 Offset, int32 ShadowOffset, int32 ParentIndex, bool& bOutReferencesChanged)
{
	SCOPE_CYCLE_COUNTER(STAT_ReaderApplyArray);

	FObjectReferencesMap* ArrayObjectReferences;
	bool bNewArrayMap = false;
	if (FObjectReferences* ExistingEntry = InObjectReferencesMap.Find(Offset))
	{
		check(ExistingEntry->Array);
		check(ExistingEntry->ParentIndex == ParentIndex && ExistingEntry->Property == Property);
		ArrayObjectReferences = ExistingEntry->Array.Get();
	}
	else
	{
		bNewArrayMap = true;
		ArrayObjectReferences = new FObjectReferencesMap();
	}

	FScriptArrayHelper ArrayHelper(Property, Data);

	int Count = GetPropertyCount(Object, FieldId, Property->Inner);
	ArrayHelper.Resize(Count);

	for (int i = 0; i < Count; i++)
	{
		int32 ElementOffset = i * Property->Inner->ElementSize;
		ApplyProperty(Object, FieldId, *ArrayObjectReferences, i, Property->Inner, ArrayHelper.GetRawPtr(i), ElementOffset, ElementOffset, ParentIndex, bOutReferencesChanged);
	}

	if (ArrayObjectReferences->Num() > 0)
	{
		if (bNewArrayMap)
		{
			// FObjectReferences takes ownership over ArrayObjectReferences
			InObjectReferencesMap.Add(Offset, FObjectReferences(ArrayObjectReferences, ShadowOffset, ParentIndex, Property));
		}
	}
	else
	{
		if (bNewArrayMap)
		{
			delete ArrayObjectReferences;
		}
		else
		{
			InObjectReferencesMap.Remove(Offset);
		}
	}
}

uint32 ComponentReader::GetPropertyCount(const Schema_Object* Object, Schema_FieldId FieldId, UProperty* Property)
{
	if (UStructProperty* StructProperty = Cast<UStructProperty>(Property))
	{
		return Schema_GetBytesCount(Object, FieldId);
	}
	else if (UBoolProperty* BoolProperty = Cast<UBoolProperty>(Property))
	{
		return Schema_GetBoolCount(Object, FieldId);
	}
	else if (UFloatProperty* FloatProperty = Cast<UFloatProperty>(Property))
	{
		return Schema_GetFloatCount(Object, FieldId);
	}
	else if (UDoubleProperty* DoubleProperty = Cast<UDoubleProperty>(Property))
	{
		return Schema_GetDoubleCount(Object, FieldId);
	}
	else if (UInt8Property* Int8Property = Cast<UInt8Property>(Property))
	{
		return Schema_GetInt32Count(Object, FieldId);
	}
	else if (UInt16Property* Int16Property = Cast<UInt16Property>(Property))
	{
		return Schema_GetInt32Count(Object, FieldId);
	}
	else if (UIntProperty* IntProperty = Cast<UIntProperty>(Property))
	{
		return Schema_GetInt32Count(Object, FieldId);
	}
	else if (UInt64Property* Int64Property = Cast<UInt64Property>(Property))
	{
		return Schema_GetInt64Count(Object, FieldId);
	}
	else if (UByteProperty* ByteProperty = Cast<UByteProperty>(Property))
	{
		return Schema_GetUint32Count(Object, FieldId);
	}
	else if (UUInt16Property* UInt16Property = Cast<UUInt16Property>(Property))
	{
		return Schema_GetUint32Count(Object, FieldId);
	}
	else if (UUInt32Property* UInt32Property = Cast<UUInt32Property>(Property))
	{
		return Schema_GetUint32Count(Object, FieldId);
	}
	else if (UUInt64Property* UInt64Property = Cast<UUInt64Property>(Property))
	{
		return Schema_GetUint64Count(Object, FieldId);
	}
	else if (UObjectPropertyBase* ObjectProperty = Cast<UObjectPropertyBase>(Property))
	{
		return Schema_GetObjectCount(Object, FieldId);
	}
	else if (UNameProperty* NameProperty = Cast<UNameProperty>(Property))
	{
		return Schema_GetBytesCount(Object, FieldId);
	}
	else if (UStrProperty* StrProperty = Cast<UStrProperty>(Property))
	{
		return Schema_GetBytesCount(Object, FieldId);
	}
	else if (UTextProperty* TextProperty = Cast<UTextProperty>(Property))
	{
		return Schema_GetBytesCount(Object, FieldId);
	}
	else if (UArrayProperty* ArrayProperty = Cast<UArrayProperty>(Property))
	{
		return GetPropertyCount(Object, FieldId, ArrayProperty->Inner);
	}
	else if (UEnumProperty* EnumProperty = Cast<UEnumProperty>(Property))
	{
		if (EnumProperty->ElementSize < 4)
		{
			return Schema_GetUint32Count(Object, FieldId);
		}
		else
		{
			return GetPropertyCount(Object, FieldId, EnumProperty->GetUnderlyingProperty());
		}
	}
	else
	{
		checkf(false, TEXT("Tried to get count of unknown property in field %d"), FieldId);
		return 0;
	}
}

} // namespace SpatialGDK
