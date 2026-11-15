using UnrealBuildTool;
public class ColossalFizzCoProductEditor : ModuleRules
{
 public ColossalFizzCoProductEditor(ReadOnlyTargetRules Target):base(Target)
 {
  PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
  PrivateDependencyModuleNames.AddRange(new[]{"Core","CoreUObject","Engine","ColossalFizzCo","Slate","SlateCore","PropertyEditor","AssetRegistry","UnrealEd","Json"});
 }
}
