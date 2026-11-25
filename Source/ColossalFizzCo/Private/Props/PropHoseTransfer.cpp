#include "Props/InteractablePropManager.h"
#include "Props/ColaPropActor.h"
#include "Components/ResourceTransferTypes.h"
#include "Components/PropGrabInputComponent.h"
#include "Shop/FillingMachineUpgradeTypes.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Curves/CurveFloat.h"
#include <cmath>

namespace HoseContent
{
    bool ValidBuffer(const FHoseResourceBuffer& Buffer)
    {
        return Buffer.Category==EResourceTransportCategory::Content && Buffer.ResourceID>0
            && FMath::IsFinite(Buffer.Units) && Buffer.Units>=0 && FMath::IsFinite(Buffer.ContentTemperature)
            && FMath::IsFinite(Buffer.ContentCreationTime) && Buffer.ContentCreationTime>=0;
    }
    bool Expired(const FPropContentDefinition& Content,double Created,double Now)
    {
        return Created>Now || (Content.ExpiryProfile.bHasExpiry && Now-Created>=Content.ExpiryProfile.ExpiryLifetimeMinutes*60.0);
    }
    bool AvailableStock(const APropManager& Manager,int32 ISID)
    {
        FShopStockEntry Entry;
        return !Manager.ShopCheckout || !Manager.ShopCheckout->GetStockEntry(ISID,Entry) || Manager.ShopCheckout->IsAvailableShopStock(ISID);
    }
}

float APropManager::IntakeHoseContent(int32 SourceISID,FHoseResourceBuffer& Buffer,float Capacity,float Requested)
{
    if (!HasAuthority() || GetNetMode()==NM_Client || !HoseContent::ValidBuffer(Buffer)
        || !FMath::IsFinite(Capacity) || Capacity<=0 || Buffer.Units>Capacity
        || !FMath::IsFinite(Requested) || Requested<=0 || !HoseContent::AvailableStock(*this,SourceISID)) return 0;
    const auto* Record=FindISIDData(SourceISID);
    const auto* Data=FindUniqueData(SourceISID);
    const auto* Product=Record ? FindProductDefinition(Record->ProductID) : nullptr;
    const auto* Content=Data ? FindContentDefinition(Data->EventData.ContentID) : nullptr;
    if (!Record || !Data || !Product || !Content || Product->ConsumableProfile.NumOfPortions<=0
        || Data->EventData.ContentID!=Buffer.ResourceID || !FMath::IsFinite(Data->EventData.ContentLevel) || Data->EventData.ContentLevel<=0
        || !FMath::IsFinite(Data->ContinuousData.Temperature) || !FMath::IsFinite(Data->EventData.ContentCreationTime)
        || Data->EventData.ContentCreationTime<0 || HoseContent::Expired(*Content,Data->EventData.ContentCreationTime,WorldElapsedSeconds)) return 0;
    for (const auto& Pair:FillSessions) if (Pair.Value.TargetISID==SourceISID) return 0;
    const int32 Portions=Product->ConsumableProfile.NumOfPortions;
    const double Available=double(Data->EventData.ContentLevel)*Portions;
    const double Accepted=FMath::Min3(double(Requested),double(Capacity-Buffer.Units),Available);
    if (Accepted<=0) return 0;
    const float Remaining=Accepted>=Available ? 0.f : FMath::Max(0.f,Data->EventData.ContentLevel-float(Accepted/Portions));
    const float Actual=float(Available-double(Remaining)*Portions);
    if (Actual<=0 || Actual>Capacity-Buffer.Units+UE_KINDA_SMALL_NUMBER) return 0;
    const FHoseResourceBuffer Previous=Buffer;
    const double Total=double(Buffer.Units)+Actual;
    Buffer.ContentTemperature=float((double(Buffer.Units)*Buffer.ContentTemperature+double(Actual)*Data->ContinuousData.Temperature)/Total);
    Buffer.ContentCreationTime=Buffer.Units>0 ? FMath::Min(Buffer.ContentCreationTime,Data->EventData.ContentCreationTime) : Data->EventData.ContentCreationTime;
    if (Buffer.Units<=0) { Buffer.SourceISID=SourceISID; Buffer.SourceProductID=Record->ProductID; }
    Buffer.Units=float(Total);
    FPropEventSyncDelta Debit; Debit.ISID=SourceISID; Debit.SetFlag(EPropEventDirtyMask::ContentLevel); Debit.ContentLevel=Remaining;
    // Publish the buffer credit and canonical debit together to reentrant observers; roll back on rejected commit.
    if (!SubmitAtomicInstanceEventUpdates({Debit})) { Buffer=Previous; return 0; }
    return Actual;
}

float APropManager::DeliverHoseContent(FHoseResourceBuffer& Buffer,int32 TargetISID,float Requested)
{
    if (!HasAuthority() || GetNetMode()==NM_Client || !HoseContent::ValidBuffer(Buffer) || Buffer.Units<=0
        || !FMath::IsFinite(Requested) || Requested<=0 || !HoseContent::AvailableStock(*this,TargetISID)
        || !UPropGrabInputComponent::CanFillHeldBottle(*this, TargetISID)) return 0;
    const auto* Record=FindISIDData(TargetISID);
    const auto* Data=FindUniqueData(TargetISID);
    const auto* Product=Record ? FindProductDefinition(Record->ProductID) : nullptr;
    const auto* Content=FindContentDefinition(Buffer.ResourceID);
    if (!Record || !Data || !Product || !Content || Product->ConsumableProfile.NumOfPortions<=0
        || !FMath::IsFinite(Data->EventData.ContentLevel) || Data->EventData.ContentLevel<0 || !FMath::IsFinite(Data->ContinuousData.Temperature)
        || (Data->EventData.ContentLevel>0 && Data->EventData.ContentID!=Buffer.ResourceID)
        || Product->GetEffectiveStrength(*Data)<=.7f || !Data->EventData.bClean || Data->EventData.CapSealed>0 || Data->EventData.AttachedCapISID>0
        || (!Data->EventData.bTemporarySealArmed && Data->EventData.TemporarySealUntil>WorldElapsedSeconds)
        || !IsAssemblyCompatible(Record->ProductID,Buffer.ResourceID,0) || HoseContent::Expired(*Content,Buffer.ContentCreationTime,WorldElapsedSeconds)) return 0;
    for (const auto& Pair:FillSessions) if (Pair.Value.SourceISID==TargetISID || Pair.Value.TargetISID==TargetISID) return 0;
    const bool Deforms=Product->BottleMaterialType==EBottleMaterialType::Plastic || Product->BottleMaterialType==EBottleMaterialType::HeavyMetal;
    const float Capacity=Product->MaxOverfillLevel*(Deforms ? FMath::Clamp(Product->GetEffectiveStrength(*Data),0.f,1.f) : 1.f);
    const float Flow=Product->FillSlowdownCurve ? Product->FillSlowdownCurve->GetFloatValue(Data->EventData.ContentLevel) : 1.f;
    const int32 Portions=Product->ConsumableProfile.NumOfPortions;
    if (!FMath::IsFinite(Capacity) || !FMath::IsFinite(Flow) || Flow<=0 || Data->EventData.ContentLevel>=Capacity) return 0;
    const double Accepted=FMath::Min3(double(Requested)*FMath::Min(1.f,Flow),double(Buffer.Units),double(Capacity-Data->EventData.ContentLevel)*Portions);
    if (Accepted<=0) return 0;
    if (Record->Representation==EPropRepresentation::ISM)
    {
        AActivePropActor* Active=nullptr;
        if (!PromoteInstanceToASM(TargetISID,EPropActivationReason::Interaction,true,Active)) return 0;
    }
    const auto* Bottle=Cast<AColaPropActor>(GetActiveActorFromISID(TargetISID));
    if (Bottle && !Bottle->CanAcceptManufacturingContent(Buffer.ResourceID)) return 0;
    Data=FindUniqueData(TargetISID);
    if (!Data) return 0;
    const float PreviousLevel=Data->EventData.ContentLevel;
    const float NewLevel=FMath::Min(Capacity,PreviousLevel+float(Accepted/Portions));
    const float Actual=(NewLevel-PreviousLevel)*Portions;
    if (Actual<=0 || Actual>Buffer.Units+UE_KINDA_SMALL_NUMBER) return 0;
    const double Existing=double(PreviousLevel)*Portions;
    const float Cooling=Bottle ? Bottle->GetActiveFillingUpgradeValue(EFillingMachineUpgradeType::TemperatureRegulator) : 0.f;
    const float Temperature=float((Existing*Data->ContinuousData.Temperature
        +double(Actual)*(Buffer.ContentTemperature-Cooling))/(Existing+Actual));
    FPropEventSyncDelta Credit; Credit.ISID=TargetISID; Credit.SetFlag(EPropEventDirtyMask::ContentLevel); Credit.SetFlag(EPropEventDirtyMask::ContentID);
    Credit.ContentLevel=NewLevel; Credit.ContentID=Buffer.ResourceID;
    Credit.ExtendedDirtyMask=EPropExtendedEventMask::ContentCreationTime;
    Credit.ContentCreationTime=PreviousLevel>0 ? FMath::Min(Data->EventData.ContentCreationTime,Buffer.ContentCreationTime) : Buffer.ContentCreationTime;
    const FHoseResourceBuffer Previous=Buffer;
    Buffer.Units=FMath::Max(0.f,Buffer.Units-Actual);
    if (!SubmitAtomicInstanceEventUpdates({Credit})) { Buffer=Previous; return 0; }
    SetTemperature(TargetISID,Temperature);
    if (const auto* Updated=FindUniqueData(TargetISID))
    {
        FPropContinuousSyncItem Item; Item.ISID=TargetISID; Item.Temperature=Updated->ContinuousData.Temperature; Item.SealedPressure=Updated->ContinuousData.SealedPressure;
        Multicast_SyncContinuousBatch(WorldElapsedSeconds,{Item});
    }
    if (auto* UpdatedBottle=Cast<AColaPropActor>(GetActiveActorFromISID(TargetISID))) UpdatedBottle->OnManufacturingLiquidReceived(PreviousLevel);
    return Actual;
}

float APropManager::ReleaseHoseContent(FHoseResourceBuffer& Buffer,FGuid ReleaseEpisode,FVector Location,float RequestedUnits,bool bContinueEpisode)
{
    if (!HasAuthority() || GetNetMode()==NM_Client || !HoseContent::ValidBuffer(Buffer) || Buffer.Units<=0
        || !FMath::IsFinite(RequestedUnits) || RequestedUnits<=0.f || !ReleaseEpisode.IsValid()
        || Location.ContainsNaN() || Buffer.SourceISID<0 || Buffer.SourceProductID<=0
        || !FindContentDefinition(Buffer.ResourceID)) return 0;
    if (Buffer.SourceISID==0 && Buffer.SourceProductID!=Buffer.ResourceID && !FindProductDefinition(Buffer.SourceProductID)) return 0;
    const int32 Existing=ReleasedLiquids.IndexOfByPredicate([&](const auto& Item){return Item.ReleaseID==ReleaseEpisode;});
    FReleasedLiquid Liquid=Existing!=INDEX_NONE ? ReleasedLiquids[Existing] : FReleasedLiquid();
    if (Existing!=INDEX_NONE && (!bContinueEpisode || Liquid.SourceISID!=Buffer.SourceISID
        || Liquid.SourceProductID!=Buffer.SourceProductID || Liquid.ContentID!=Buffer.ResourceID)) return 0;
    float Remaining=FMath::Max(0.f,Buffer.Units-RequestedUnits);
    if (Buffer.Units-Remaining>RequestedUnits) Remaining=std::nextafter(Remaining,Buffer.Units);
    const float Released=Buffer.Units-Remaining;
    if (Released<=0.f) return 0;
    Liquid.ReleaseID=ReleaseEpisode; Liquid.SourceISID=Buffer.SourceISID; Liquid.SourceProductID=Buffer.SourceProductID;
    Liquid.ContentID=Buffer.ResourceID;
    Liquid.Temperature=float((Liquid.Portions*Liquid.Temperature+double(Released)*Buffer.ContentTemperature)/(Liquid.Portions+Released));
    Liquid.ContentCreationTime=Liquid.Portions>0. ? FMath::Min(Liquid.ContentCreationTime,Buffer.ContentCreationTime) : Buffer.ContentCreationTime;
    Liquid.Portions+=Released; Liquid.ReleaseTime=WorldElapsedSeconds; Liquid.Transform=FTransform(Location);
    if (!ValidateReleasedLiquids({Liquid},WorldElapsedSeconds)) return 0;
    Buffer.Units=Remaining;
    if (Existing!=INDEX_NONE) ReleasedLiquids[Existing]=Liquid; else ReleasedLiquids.Add(Liquid);
    OnLiquidReleasesChanged.Broadcast(); ForceNetUpdate(); return Released;
}
