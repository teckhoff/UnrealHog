// Trevor Eckhoff, 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "UObject/UnrealType.h"

// Shared reflection helpers for automation tests. These live in a named namespace rather
// than per-file anonymous namespaces because unity builds merge test translation units,
// which turns duplicated anonymous-namespace definitions into redefinition errors.
namespace UnrealHogTests
{
	template<typename T>
	void SetPropertyValue(UObject* Object, const TCHAR* PropertyName, const T& Value)
	{
		FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), PropertyName);
		check(Property);
		T* ValuePtr = Property->ContainerPtrToValuePtr<T>(Object);
		*ValuePtr = Value;
	}

	template<typename T>
	T GetPropertyValue(const UObject* Object, const TCHAR* PropertyName)
	{
		const FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), PropertyName);
		check(Property);
		return *Property->ContainerPtrToValuePtr<T>(Object);
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
