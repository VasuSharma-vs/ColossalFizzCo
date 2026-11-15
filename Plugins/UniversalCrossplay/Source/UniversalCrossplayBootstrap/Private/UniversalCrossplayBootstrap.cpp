#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"

// Set the engine-supported runtime selection BEFORE EOSShared's PostConfigInit.
// EOSShared owns SDK initialization, platform handles, ticking and shutdown.
class FUniversalCrossplayBootstrap:public IModuleInterface
{
public:
 virtual void StartupModule() override
 {
  const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("UniversalCrossplay"));
  if (!Plugin) return;
  const FString Library=FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir()/TEXT("Binaries/ThirdParty/Win64/EOSSDK-Win64-Shipping.dll"));
  if (IFileManager::Get().FileSize(*Library)<=0)
  {UE_LOG(LogTemp,Error,TEXT("UniversalCrossplay: supplied EOS runtime is missing. Run the plugin SDK installer."));return;}
  FString Existing;
  if (!FParse::Value(FCommandLine::Get(),TEXT("eossdkbinary="),Existing))
  {
   const FString Argument=FString::Printf(TEXT(" -eossdkbinary=\"%s\""),*Library);
   FCommandLine::Append(*Argument);
  }
  UE_LOG(LogTemp,Display,TEXT("UniversalCrossplay: project-local EOS runtime selected before EOSShared startup."));
 }
};
IMPLEMENT_MODULE(FUniversalCrossplayBootstrap,UniversalCrossplayBootstrap)
