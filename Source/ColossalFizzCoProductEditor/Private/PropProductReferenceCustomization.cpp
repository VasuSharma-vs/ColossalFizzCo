#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IPropertyTypeCustomization.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "DetailWidgetRow.h"
#include "Props/PropProductCatalog.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
#endif

class FPropProductReferenceCustomization final:public IPropertyTypeCustomization
{
#if WITH_DEV_AUTOMATION_TESTS
 friend class FProductPickerEditorTest;
#endif
 TSharedPtr<IPropertyHandle> Value;
 TSharedPtr<SComboButton> Picker;
 TSharedPtr<SVerticalBox> Choices;
 TArray<TStrongObjectPtr<UPropProductCatalog>> Catalogs;
 FString Filter,Search;
 void LoadCatalogs()
 {
  Catalogs.Reset();FARFilter Query;Query.ClassPaths.Add(UPropProductCatalog::StaticClass()->GetClassPathName());Query.bRecursiveClasses=true;
  TArray<FAssetData> Assets;FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().GetAssets(Query,Assets);
  for(const auto& A:Assets)if(auto* C=Cast<UPropProductCatalog>(A.GetAsset()))Catalogs.Emplace(C);
 }
 FText Label()const
 {
  int32 PID=0;Value->GetValue(PID);if(!PID)return FText::FromString(TEXT("None"));
  for(const auto& C:Catalogs)if(const auto* Entry=C->Find(PID))return Entry->Name;
  return FText::FromString(FString::Printf(TEXT("Missing product (%d)"),PID));
 }
 bool CategoryMatches(const FPropCatalogEntry& Entry)const
 {return Filter.IsEmpty() || StaticEnum<EPropProductCategory>()->GetNameStringByValue(int64(Entry.Category)).Equals(Filter,ESearchCase::IgnoreCase);}
 void Choose(int32 PID)
 {Value->SetValue(PID);if(Picker)Picker->SetIsOpen(false);}
 void Refresh()
 {
  if(!Choices)return;Choices->ClearChildren();
  Choices->AddSlot().AutoHeight()[SNew(SButton).Text(FText::FromString(TEXT("None"))).OnClicked_Lambda([this]{Choose(0);return FReply::Handled();})];
  TSet<int32> Seen;
  for(const auto& C:Catalogs)for(const auto& Entry:C->Products)
  {
   if(Entry.CanonicalPID || !CategoryMatches(Entry) || Seen.Contains(Entry.PID))continue;
   const FString Name=Entry.Name.ToString();
   if(!Search.IsEmpty() && !Name.Contains(Search,ESearchCase::IgnoreCase) && !FString::FromInt(Entry.PID).Contains(Search))continue;
   Seen.Add(Entry.PID);const int32 PID=Entry.PID;
   Choices->AddSlot().AutoHeight().Padding(0,2)[SNew(SButton).Text(Entry.Name)
    .ToolTipText(FText::FromString(FString::Printf(TEXT("PID %d — %s"),PID,*Entry.Definition.ToSoftObjectPath().ToString())))
    .OnClicked_Lambda([this,PID]{Choose(PID);return FReply::Handled();})];
  }
  if(Seen.IsEmpty())Choices->AddSlot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("No registered products in this category. Check the product catalog."))).AutoWrapText(true)];
 }
 TSharedRef<SWidget> Menu()
 {
  LoadCatalogs();Search.Reset();
  auto Widget=SNew(SBox).WidthOverride(360).HeightOverride(360)[SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight()[SNew(SSearchBox).HintText(FText::FromString(TEXT("Search registered product names")))
    .OnTextChanged_Lambda([this](const FText& Text){Search=Text.ToString();Refresh();})]
   +SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(Choices,SVerticalBox)]]];
  Refresh();return Widget;
 }
public:
 static TSharedRef<IPropertyTypeCustomization> Make(){return MakeShared<FPropProductReferenceCustomization>();}
 virtual void CustomizeHeader(TSharedRef<IPropertyHandle> Property,FDetailWidgetRow& Row,IPropertyTypeCustomizationUtils&)override
 {
  Value=Property->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPropProductReference,PID));
  for(TSharedPtr<IPropertyHandle> P=Property;P.IsValid();P=P->GetParentHandle())
   if(P->HasMetaData(TEXT("ProductCategory"))){Filter=P->GetMetaData(TEXT("ProductCategory"));break;}
  LoadCatalogs();Row.NameContent()[Property->CreatePropertyNameWidget()]
   .ValueContent().MinDesiredWidth(240)[SAssignNew(Picker,SComboButton).OnGetMenuContent(this,&FPropProductReferenceCustomization::Menu)
    .ButtonContent()[SNew(STextBlock).Text(this,&FPropProductReferenceCustomization::Label)]];
 }
 virtual void CustomizeChildren(TSharedRef<IPropertyHandle>,IDetailChildrenBuilder&,IPropertyTypeCustomizationUtils&)override{}
};

class FColossalFizzCoProductEditor final:public IModuleInterface
{
public:
 virtual void StartupModule()override
 {auto& Properties=FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");Properties.RegisterCustomPropertyTypeLayout(TEXT("PropProductReference"),FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPropProductReferenceCustomization::Make));Properties.NotifyCustomizationModuleChanged();}
 virtual void ShutdownModule()override
 {if(FModuleManager::Get().IsModuleLoaded("PropertyEditor")){auto& Properties=FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");Properties.UnregisterCustomPropertyTypeLayout(TEXT("PropProductReference"));Properties.NotifyCustomizationModuleChanged();}}
};
IMPLEMENT_MODULE(FColossalFizzCoProductEditor,ColossalFizzCoProductEditor)

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProductPickerEditorTest,"ColossalFizzCo.Products.EditorNamedPicker",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProductPickerEditorTest::RunTest(const FString&)
{
 auto* Catalog=LoadObject<UPropProductCatalog>(nullptr,TEXT("/Game/Props/Products/DA_ProductCatalog.DA_ProductCatalog"));
 if(!TestNotNull(TEXT("Authored identity catalog loads in editor"),Catalog))return false;
 auto Picker=MakeShared<FPropProductReferenceCustomization>();Picker->Menu();
 auto Count=[&](const FString& Category,const FString& Search)
 {
  Picker->Filter=Category;Picker->Search=Search;Picker->Refresh();
  int32 Count=0;for(int32 I=0;I<Picker->Choices->GetChildren()->Num();++I)
   if(Picker->Choices->GetChildren()->GetChildAt(I)->GetTypeAsString()==TEXT("SButton"))++Count;
  return Count-1; // The explicit None button is always present.
 };
 TestEqual(TEXT("Bottle selection contains only the registered bottle"),Count(TEXT("Bottle"),TEXT("")),1);
 TestEqual(TEXT("A cola name cannot appear in bottle-only selection"),Count(TEXT("Bottle"),TEXT("Spicy")),0);
 TestEqual(TEXT("Content selection discovers all five Normal products"),Count(TEXT("Content"),TEXT("")),5);
 TestEqual(TEXT("Content names are searchable"),Count(TEXT("Content"),TEXT("Spicy")),1);
 TestEqual(TEXT("Cap selection excludes contents and bottles"),Count(TEXT("Cap"),TEXT("")),1);
 const auto* Caps=FindFProperty<FArrayProperty>(UPropCapDataAsset::StaticClass(),TEXT("SupportedBottles"));
 TestTrue(TEXT("Cap array carries the bottle category filter"),Caps && Caps->GetMetaData(TEXT("ProductCategory"))==TEXT("Bottle"));
 return true;
}
#endif
