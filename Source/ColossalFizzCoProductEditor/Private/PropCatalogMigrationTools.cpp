#include "PropCatalogMigrationTools.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Props/InteractablePropDatabase.h"
#include "Props/InteractablePropDataAsset.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
USaveGame* UPropCatalogMigrationTools::ReadSaveCopy(const FString& File)
{
 FString Full=FPaths::ConvertRelativePathToFull(File);FPaths::NormalizeFilename(Full);FPaths::CollapseRelativeDirectories(Full);
 FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());FPaths::NormalizeDirectoryName(Root);Root+=TEXT("/");
 if(!Full.StartsWith(Root) || FPaths::GetExtension(Full)!=TEXT("sav"))return nullptr;
 TArray<uint8> Bytes;return FFileHelper::LoadFileToArray(Bytes,*Full) ? UGameplayStatics::LoadGameFromMemory(Bytes) : nullptr;
}
FString UPropCatalogMigrationTools::InspectSavedIdentities(USaveGame* Save)
{
 const auto* Data=Cast<UPropDatabaseSaveGame>(Save);if(!Data)return TEXT("{}");
 auto Root=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Products,Contents;
 for(const auto& D:Data->WorldSaveData.ProductDefinitions)
 {
  auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("pid"),D.ProductID);R->SetStringField(TEXT("name"),D.DisplayName.ToString());
  R->SetStringField(TEXT("source"),GetPathNameSafe(D.SourceDataAsset));R->SetNumberField(TEXT("portions"),D.ConsumableProfile.NumOfPortions);
  R->SetBoolField(TEXT("liquid_only"),D.bLiquidVisualOnly);R->SetBoolField(TEXT("cap"),D.bIsBottleCap);
  R->SetNumberField(TEXT("content"),D.DefaultUniqueData.EventData.ContentID);R->SetNumberField(TEXT("liquid_pid"),D.LiquidVisualPID);
  Products.Add(MakeShared<FJsonValueObject>(R));
 }
 for(const auto& D:Data->WorldSaveData.ContentDefinitions)
 {auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("pid"),D.ContentID);R->SetStringField(TEXT("name"),D.DisplayName.ToString());Contents.Add(MakeShared<FJsonValueObject>(R));}
 Root->SetArrayField(TEXT("definitions"),Products);Root->SetArrayField(TEXT("contents"),Contents);
 Root->SetNumberField(TEXT("instance_count"),Data->WorldSaveData.SavedInstances.Num());
 FString Result;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Result));return Result;
}
