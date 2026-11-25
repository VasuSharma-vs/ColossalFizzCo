#include "Shop/ShopPlayerInteractionComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

void UShopPlayerInteractionComponent::ShowProductionResult(EShopCheckoutResult Result)
{
 if(Result!=EShopCheckoutResult::ContentNotSupported && Result!=EShopCheckoutResult::CapNotSupported)return;
 auto* PC=Cast<APlayerController>(GetOwner());if(const auto* Pawn=Cast<APawn>(GetOwner()))PC=Cast<APlayerController>(Pawn->GetController());
 auto* Local=PC && PC->IsLocalController() ? PC->GetLocalPlayer() : nullptr;
 if(!Local || !Local->ViewportClient || !GetWorld())return;
 ClearProductionNotice();NoticePlayer=Local;
 const FText Text=Result==EShopCheckoutResult::ContentNotSupported ? NSLOCTEXT("Production","UnsupportedContent","Content not supported") : NSLOCTEXT("Production","UnsupportedCap","Cap not supported");
 ProductionNotice=SNew(SOverlay).Visibility(EVisibility::HitTestInvisible)
  +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(24,36)
  [SNew(SBorder).Padding(14).BorderBackgroundColor(FLinearColor(.02f,.02f,.02f,.85f))
   [SNew(STextBlock).Text(Text).ColorAndOpacity(FLinearColor(1,.05f,.05f)).Font(FCoreStyle::GetDefaultFontStyle("Bold",22))]];
 Local->ViewportClient->AddViewportWidgetForPlayer(Local,ProductionNotice.ToSharedRef(),100);
 GetWorld()->GetTimerManager().SetTimer(NoticeTimer,this,&UShopPlayerInteractionComponent::ClearProductionNotice,4.f,false);
}
void UShopPlayerInteractionComponent::ClearProductionNotice()
{
 if(auto* Local=NoticePlayer.Get();Local && Local->ViewportClient && ProductionNotice)Local->ViewportClient->RemoveViewportWidgetForPlayer(Local,ProductionNotice.ToSharedRef());
 ProductionNotice.Reset();NoticePlayer.Reset();if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(NoticeTimer);
}
void UShopPlayerInteractionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (auto* Pawn=Cast<APawn>(GetOwner()))
		Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(this,&UShopPlayerInteractionComponent::OnFillingPawnControllerChanged);
	UnbindFillingTableInput();
	CleanupFlowLeverInput();
	CancelSocketUnplugHold();
	CleanupCableGrab();
	ClearProductionNotice();
	Super::EndPlay(Reason);
}
