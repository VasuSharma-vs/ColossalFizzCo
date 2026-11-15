using UnrealBuildTool;
using System.IO;
public class UniversalCrossplay:ModuleRules
{
 public UniversalCrossplay(ReadOnlyTargetRules Target):base(Target)
 {
  PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
  PublicDependencyModuleNames.AddRange(new[]{"Core","CoreUObject","CoreOnline","Engine","DeveloperSettings","OnlineSubsystem","OnlineSubsystemUtils","NetCore","UMG"});
  PrivateDependencyModuleNames.AddRange(new[]{"OnlineSubsystemEOS","EOSShared","EOSSDK","SocketSubsystemEOS","Sockets","VoiceChat","EOSVoiceChat","SteamShared","Slate","SlateCore","Projects"});
  AddEngineThirdPartyPrivateStaticDependencies(Target,"Steamworks");
  string sdk=Path.GetFullPath(Path.Combine(ModuleDirectory,"../../ThirdParty/EOSSDK"));
  PublicAdditionalLibraries.Add(Path.Combine(sdk,"Lib/EOSSDK-Win64-Shipping.lib"));
 }
}
