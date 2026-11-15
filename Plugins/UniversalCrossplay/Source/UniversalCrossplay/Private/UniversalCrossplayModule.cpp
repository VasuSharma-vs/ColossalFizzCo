#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "CrossplaySampleWidget.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/CoreDelegates.h"
#include "CrossplaySettings.h"
namespace
{
 FAutoConsoleCommandWithWorld Menu(TEXT("Crossplay.Menu"),TEXT("Open the optional reusable multiplayer sample menu."),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
 {if(W)for(auto It=W->GetPlayerControllerIterator();It;++It)if(auto* P=It->Get();P && P->IsLocalController()){UCrossplaySampleUI::OpenMenu(P);break;}}));
}
class FUniversalCrossplayModule:public IModuleInterface
{
 FDelegateHandle EngineReady;
 static void ConfigureTransport()
 {
  if(!GEngine || !GetDefault<UCrossplaySettings>()->bEnableOnline)return;
  auto* Def=GEngine->NetDriverDefinitions.FindByPredicate([](const auto& D){return D.DefName==NAME_GameNetDriver;});
  if(!Def){FNetDriverDefinition D;D.DefName=NAME_GameNetDriver;GEngine->NetDriverDefinitions.Add(D);Def=&GEngine->NetDriverDefinitions.Last();}
  Def->DriverClassName=FName(TEXT("/Script/SocketSubsystemEOS.NetDriverEOS"));Def->DriverClassNameFallback=FName(TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"));
 }
public:
 virtual void StartupModule() override {EngineReady=FCoreDelegates::GetOnPostEngineInit().AddStatic(&FUniversalCrossplayModule::ConfigureTransport);if(GEngine)ConfigureTransport();}
 virtual void ShutdownModule() override {FCoreDelegates::GetOnPostEngineInit().Remove(EngineReady);}
};
IMPLEMENT_MODULE(FUniversalCrossplayModule,UniversalCrossplay)
