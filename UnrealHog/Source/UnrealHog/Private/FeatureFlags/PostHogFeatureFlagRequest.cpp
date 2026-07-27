#include "FeatureFlags/PostHogFeatureFlagRequest.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace PostHogFeatureFlagRequestFields
{
	const TCHAR* const ApiKeyFieldName = TEXT("api_key");
	const TCHAR* const DistinctIdFieldName = TEXT("distinct_id");
	const TCHAR* const AnonymousIdFieldName = TEXT("$anon_distinct_id");
	const TCHAR* const GroupsFieldName = TEXT("$groups");
	const TCHAR* const PersonPropertiesFieldName = TEXT("person_properties");
	const TCHAR* const GroupPropertiesFieldName = TEXT("group_properties");

	// Builds an object from raw JSON values, preserving nested types. Null values are skipped so a
	// caller-side gap can never serialize as an invalid field.
	TSharedRef<FJsonObject> MakePropertyObject(const TMap<FString, TSharedPtr<FJsonValue>>& Properties)
	{
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Property : Properties)
		{
			if (Property.Value.IsValid())
			{
				Object->SetField(Property.Key, Property.Value);
			}
		}

		return Object;
	}
}

TSharedRef<FJsonObject> FPostHogFeatureFlagRequest::ToJsonObject() const
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(PostHogFeatureFlagRequestFields::ApiKeyFieldName, ApiKey);
	Body->SetStringField(PostHogFeatureFlagRequestFields::DistinctIdFieldName, DistinctId);

	if (!AnonymousId.IsEmpty())
	{
		Body->SetStringField(PostHogFeatureFlagRequestFields::AnonymousIdFieldName, AnonymousId);
	}

	if (Groups.Num() > 0)
	{
		const TSharedRef<FJsonObject> GroupsObject = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& Group : Groups)
		{
			GroupsObject->SetStringField(Group.Key, Group.Value);
		}
		Body->SetObjectField(PostHogFeatureFlagRequestFields::GroupsFieldName, GroupsObject);
	}

	if (PersonProperties.Num() > 0)
	{
		Body->SetObjectField(PostHogFeatureFlagRequestFields::PersonPropertiesFieldName, PostHogFeatureFlagRequestFields::MakePropertyObject(PersonProperties));
	}

	if (GroupProperties.Num() > 0)
	{
		const TSharedRef<FJsonObject> GroupPropertiesObject = MakeShared<FJsonObject>();
		for (const TPair<FString, TMap<FString, TSharedPtr<FJsonValue>>>& GroupEntry : GroupProperties)
		{
			GroupPropertiesObject->SetObjectField(GroupEntry.Key, PostHogFeatureFlagRequestFields::MakePropertyObject(GroupEntry.Value));
		}
		Body->SetObjectField(PostHogFeatureFlagRequestFields::GroupPropertiesFieldName, GroupPropertiesObject);
	}

	return Body;
}

bool FPostHogFeatureFlagRequest::ToJsonString(FString& OutJson) const
{
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	return FJsonSerializer::Serialize(ToJsonObject(), Writer);
}
