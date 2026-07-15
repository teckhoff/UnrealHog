// Trevor Eckhoff, 2026. All rights reserved.

#include "Events/PostHogEventRehydration.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	PostHogEventRehydration::FResult Failure(const FString& Diagnostic)
	{
		PostHogEventRehydration::FResult Result;
		Result.Diagnostic = Diagnostic;
		return Result;
	}

	bool TryReadRequiredString(const TSharedRef<FJsonObject>& JsonObject, const TCHAR* FieldName, FString& OutValue, FString& OutDiagnostic)
	{
		OutValue.Empty();

		if (!JsonObject->HasTypedField<EJson::String>(FieldName))
		{
			OutDiagnostic = FString::Printf(TEXT("Missing or non-string required field '%s'."), FieldName);
			return false;
		}

		OutValue = JsonObject->GetStringField(FieldName);
		if (OutValue.IsEmpty())
		{
			OutDiagnostic = FString::Printf(TEXT("Required field '%s' must not be empty."), FieldName);
			return false;
		}

		return true;
	}
}

bool PostHogEventRehydration::FResult::IsSuccess() const
{
	return Event.IsSet() && Diagnostic.IsEmpty();
}

PostHogEventRehydration::FResult PostHogEventRehydration::TryParsePersistedEventJson(const FString& EventJson)
{
	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(EventJson);

	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		return Failure(TEXT("Persisted event JSON must be a valid JSON object."));
	}

	const TSharedRef<FJsonObject> RootObjectRef = RootObject.ToSharedRef();

	FString EventUuid;
	FString EventName;
	FString DistinctId;
	FString Timestamp;
	FString Diagnostic;

	if (!TryReadRequiredString(RootObjectRef, TEXT("uuid"), EventUuid, Diagnostic)
		|| !TryReadRequiredString(RootObjectRef, TEXT("event"), EventName, Diagnostic)
		|| !TryReadRequiredString(RootObjectRef, TEXT("distinct_id"), DistinctId, Diagnostic)
		|| !TryReadRequiredString(RootObjectRef, TEXT("timestamp"), Timestamp, Diagnostic))
	{
		return Failure(Diagnostic);
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	if (!RootObjectRef->TryGetObjectField(TEXT("properties"), PropertiesObject) || !PropertiesObject || !PropertiesObject->IsValid())
	{
		return Failure(TEXT("Missing or non-object required field 'properties'."));
	}

	FResult Result;
	Result.Event = FPostHogEvent(FPostHogEvent::FRehydratedEventTag(), EventUuid, EventName, DistinctId, Timestamp, **PropertiesObject);
	return Result;
}
