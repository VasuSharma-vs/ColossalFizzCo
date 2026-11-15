#pragma once
#include "CoreMinimal.h"
#include "CrossplayTypes.generated.h"
UENUM(BlueprintType) enum class ECrossplayStore:uint8 { Auto,Steam,Epic,Development };
UENUM(BlueprintType) enum class ECrossplayPhase:uint8 { Offline,Authenticating,Online,Creating,Searching,Joining,Lobby,Starting,Playing,Leaving,Error };
USTRUCT(BlueprintType) struct UNIVERSALCROSSPLAY_API FCrossplayIdentity
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") FString OnlineId;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") FString ProductUserId;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") FString DisplayName;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") ECrossplayStore Store=ECrossplayStore::Auto;
};
USTRUCT(BlueprintType) struct UNIVERSALCROSSPLAY_API FCrossplayLobby
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") FString Id;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") FString Name;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") FString OwnerName;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") int32 Players=0;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") int32 Capacity=2;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") bool bPrivate=false;
};
USTRUCT(BlueprintType) struct UNIVERSALCROSSPLAY_API FCrossplayMember
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") FString OnlineId;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") FString Name;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") FString Platform;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") bool bReady=false;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") bool bOwner=false;
};
USTRUCT(BlueprintType) struct UNIVERSALCROSSPLAY_API FCrossplayFriend
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") FString Id;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") FString Name;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") ECrossplayStore Store=ECrossplayStore::Auto;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") bool bOnline=false;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") bool bJoinable=false;
};
USTRUCT(BlueprintType) struct UNIVERSALCROSSPLAY_API FCrossplayDevice
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") FString Id;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") FString Name;
};
USTRUCT(BlueprintType) struct UNIVERSALCROSSPLAY_API FCrossplayChatLine
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") FCrossplayMember Sender;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") FString Message;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") int64 Sequence=0;
};
// Enforce a small wire allocation BEFORE decoding any untrusted text.
USTRUCT() struct UNIVERSALCROSSPLAY_API FCrossplayChatPayload
{
 GENERATED_BODY()
 FString Text;
 bool NetSerialize(FArchive& Ar,UPackageMap*,bool& Success);
};
template<> struct TStructOpsTypeTraits<FCrossplayChatPayload>:TStructOpsTypeTraitsBase2<FCrossplayChatPayload> { enum {WithNetSerializer=true}; };
struct UNIVERSALCROSSPLAY_API FCrossplayChatPolicy
{
 static bool Validate(const FString& Text,FString& Clean);
 bool Accept(uint32 Sequence,double Now);
private:
 uint32 LastSequence=0; double LastTime=0; double Tokens=4;
};
