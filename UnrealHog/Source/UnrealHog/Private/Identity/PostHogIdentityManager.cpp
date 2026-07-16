#include "Identity/PostHogIdentityManager.h"

#include "Dom/JsonObject.h"
#include "Logging/PostHogLogger.h"
#include "Logging/StructuredLog.h"
#include "Serialization/JsonSerializer.h"
#include "Storage/PostHogStorageProvider.h"
#include "Utilities/PostHogUuidV7.h"

namespace
{
	const FString AnonymousIdFieldName = TEXT("anonymous_id");
	const FString DistinctIdFieldName = TEXT("distinct_id");
	const FString IsIdentifiedFieldName = TEXT("is_identified");
	const FString GroupsFieldName = TEXT("groups");
	const FString VersionFieldName = TEXT("version");
}

const TCHAR* const FPostHogIdentityManager::StateKey = TEXT("identity");

FPostHogIdentityManager::FPostHogIdentityManager(FUuidGenerator InUuidGenerator) :
	UuidGenerator(MoveTemp(InUuidGenerator))
{
}

void FPostHogIdentityManager::LoadOrCreate(IPostHogStorageProvider& Storage)
{
	FString StateJson;
	if (Storage.LoadState(StateKey, StateJson))
	{
		TSharedPtr<FJsonObject> StateObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(StateJson);

		int32 Version = 0;
		FString LoadedAnonymousId;

		if (FJsonSerializer::Deserialize(Reader, StateObject) && StateObject.IsValid()
			&& StateObject->TryGetNumberField(VersionFieldName, Version)
			&& Version == CurrentSchemaVersion
			&& StateObject->TryGetStringField(AnonymousIdFieldName, LoadedAnonymousId)
			&& !LoadedAnonymousId.IsEmpty())
		{
			AnonymousId = LoadedAnonymousId;
			DistinctId.Empty();
			StateObject->TryGetStringField(DistinctIdFieldName, DistinctId);
			StateObject->TryGetBoolField(IsIdentifiedFieldName, bIsIdentified);

			Groups.Empty();
			const TSharedPtr<FJsonObject>* GroupsObject = nullptr;
			if (StateObject->TryGetObjectField(GroupsFieldName, GroupsObject) && GroupsObject->IsValid())
			{
				for (const auto& GroupPair : (*GroupsObject)->Values)
				{
					FString GroupValue;
					if (GroupPair.Value.IsValid() && GroupPair.Value->TryGetString(GroupValue))
					{
						Groups.Add(FString(*GroupPair.Key), GroupValue);
					}
				}
			}

			return;
		}

		UE_LOGFMT(LogPostHog, Warning, "PostHog Identity Manager found missing, malformed, or unsupported-version identity state; generating a fresh anonymous identity.");
	}

	AnonymousId = UuidGenerator ? UuidGenerator() : PostHogUuidV7::New();
	DistinctId.Empty();
	bIsIdentified = false;
	Groups.Empty();

	PersistState(Storage);
}

FString FPostHogIdentityManager::Identify(const FString& NewDistinctId, IPostHogStorageProvider& Storage)
{
	const FString PreviousAnonymousId = bIsIdentified ? FString() : AnonymousId;

	DistinctId = NewDistinctId;
	bIsIdentified = true;

	PersistState(Storage);

	return PreviousAnonymousId;
}

void FPostHogIdentityManager::Reset(IPostHogStorageProvider& Storage, bool bReuseAnonymousId)
{
	DistinctId.Empty();
	bIsIdentified = false;
	Groups.Empty();

	if (!bReuseAnonymousId)
	{
		AnonymousId = UuidGenerator ? UuidGenerator() : PostHogUuidV7::New();
	}

	PersistState(Storage);
}

void FPostHogIdentityManager::PersistState(IPostHogStorageProvider& Storage)
{
	const TSharedRef<FJsonObject> StateObject = MakeShared<FJsonObject>();
	StateObject->SetNumberField(VersionFieldName, CurrentSchemaVersion);
	StateObject->SetStringField(AnonymousIdFieldName, AnonymousId);
	StateObject->SetStringField(DistinctIdFieldName, DistinctId);
	StateObject->SetBoolField(IsIdentifiedFieldName, bIsIdentified);

	const TSharedRef<FJsonObject> GroupsObject = MakeShared<FJsonObject>();
	for (const auto& GroupPair : Groups)
	{
		GroupsObject->SetStringField(GroupPair.Key, GroupPair.Value);
	}
	StateObject->SetObjectField(GroupsFieldName, GroupsObject);

	Storage.SaveState(StateKey, StateObject);
}
