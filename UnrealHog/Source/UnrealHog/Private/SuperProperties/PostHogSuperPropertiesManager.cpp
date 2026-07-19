#include "SuperProperties/PostHogSuperPropertiesManager.h"

#include "Dom/JsonObject.h"
#include "Events/PostHogCapturePolicy.h"
#include "Events/PostHogEvent.h"
#include "Events/PostHogPropertyJson.h"
#include "Logging/PostHogLogger.h"
#include "Logging/StructuredLog.h"
#include "Serialization/JsonSerializer.h"
#include "Storage/PostHogStorageProvider.h"

namespace
{
	const FString SuperPropertiesVersionFieldName = TEXT("version");
	const FString SuperPropertiesPropertiesFieldName = TEXT("properties");
}

const TCHAR* const FPostHogSuperPropertiesManager::StateKey = TEXT("super_properties");

void FPostHogSuperPropertiesManager::LoadOrCreate(IPostHogStorageProvider& Storage)
{
	Properties.Empty();

	FString StateJson;
	if (!Storage.LoadState(StateKey, StateJson))
	{
		return;
	}

	TSharedPtr<FJsonObject> StateObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(StateJson);

	int32 Version = 0;
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;

	if (FJsonSerializer::Deserialize(Reader, StateObject) && StateObject.IsValid()
		&& StateObject->TryGetNumberField(SuperPropertiesVersionFieldName, Version)
		&& Version == CurrentSchemaVersion
		&& StateObject->TryGetObjectField(SuperPropertiesPropertiesFieldName, PropertiesObject)
		&& PropertiesObject->IsValid())
	{
		for (const auto& FieldPair : (*PropertiesObject)->Values)
		{
			if (FieldPair.Key.IsEmpty() || !FieldPair.Value.IsValid())
			{
				continue;
			}
			const FString Key(*FieldPair.Key);
			Properties.Add(Key, PostHogPropertyJson::FromJsonValue(Key, *FieldPair.Value));
		}
		return;
	}

	UE_LOGFMT(LogUnrealHog, Warning, "PostHog Super Properties Manager found missing, malformed, or unsupported-version super property state; starting with no registered super properties.");
}

void FPostHogSuperPropertiesManager::Register(const FString& Key, const FPostHogEventProperty& Value, IPostHogStorageProvider& Storage)
{
	if (Key.TrimStartAndEnd().IsEmpty())
	{
		UE_LOGFMT(LogUnrealHog, Warning, "PostHog Super Properties Manager rejected Register with an empty or whitespace-only key.");
		return;
	}

	if (PostHogCapturePolicy::GetReservedPropertyKeys().Contains(Key))
	{
		UE_LOGFMT(LogUnrealHog, Warning, "Ignoring attempt to register protected PostHog property \"{Key}\" as a super property; reserved properties are SDK-owned and cannot be overwritten.", Key);
		return;
	}

	FPostHogEventProperty Copy = Value;
	Copy.Key = Key;
	Properties.Add(Key, MoveTemp(Copy));

	PersistState(Storage);
}

void FPostHogSuperPropertiesManager::Unregister(const FString& Key, IPostHogStorageProvider& Storage)
{
	if (Properties.Remove(Key) > 0)
	{
		PersistState(Storage);
	}
}

void FPostHogSuperPropertiesManager::Clear(IPostHogStorageProvider& Storage)
{
	Properties.Empty();
	PersistState(Storage);
}

void FPostHogSuperPropertiesManager::ApplyTo(FPostHogEvent& Event) const
{
	for (const auto& PropertyPair : Properties)
	{
		Event.SetJsonValueProperty(PropertyPair.Key, PostHogPropertyJson::ToJsonValue(PropertyPair.Value));
	}
}

void FPostHogSuperPropertiesManager::PersistState(IPostHogStorageProvider& Storage) const
{
	const TSharedRef<FJsonObject> PropertiesObject = MakeShared<FJsonObject>();
	for (const auto& PropertyPair : Properties)
	{
		PropertiesObject->SetField(PropertyPair.Key, PostHogPropertyJson::ToJsonValue(PropertyPair.Value));
	}

	const TSharedRef<FJsonObject> StateObject = MakeShared<FJsonObject>();
	StateObject->SetNumberField(SuperPropertiesVersionFieldName, CurrentSchemaVersion);
	StateObject->SetObjectField(SuperPropertiesPropertiesFieldName, PropertiesObject);

	Storage.SaveState(StateKey, StateObject);
}
