using UnrealBuildTool;
using System.IO;
public class UniversalCrossplayBootstrap:ModuleRules
{
 public UniversalCrossplayBootstrap(ReadOnlyTargetRules Target):base(Target)
 {
  PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
  PrivateDependencyModuleNames.AddRange(new[]{"Core","Projects"});
  string sdk=Path.GetFullPath(Path.Combine(ModuleDirectory,"../../ThirdParty/EOSSDK"));
  RuntimeDependencies.Add("$(PluginDir)/Binaries/ThirdParty/Win64/EOSSDK-Win64-Shipping.dll",Path.Combine(sdk,"Bin/EOSSDK-Win64-Shipping.dll"),StagedFileType.NonUFS);
 }
}
