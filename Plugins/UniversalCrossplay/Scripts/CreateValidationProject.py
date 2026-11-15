"""Create a disposable C++ host without game code, content or credentials."""
from pathlib import Path
import argparse
import json

def create(output):
    root = Path(output).resolve()
    plugin = Path(__file__).resolve().parents[1]
    if root.exists() and any(root.iterdir()):
        raise ValueError('Use an empty output directory; existing projects are never overwritten.')
    root.mkdir(parents=True, exist_ok=True)
    descriptor = {
        'FileVersion': 3, 'EngineAssociation': '5.8',
        'Modules': [{'Name': 'CrossplayHarness', 'Type': 'Runtime', 'LoadingPhase': 'Default'}],
        'AdditionalPluginDirectories': [str(plugin.parent)],
        'Plugins': [{'Name': 'UniversalCrossplay', 'Enabled': True}],
    }
    (root / 'CrossplayHarness.uproject').write_text(json.dumps(descriptor, indent=2) + '\n')
    files = {
        'Source/CrossplayHarness.Target.cs': 'using UnrealBuildTool;\npublic class CrossplayHarnessTarget : TargetRules { public CrossplayHarnessTarget(TargetInfo T):base(T) { Type=TargetType.Game; DefaultBuildSettings=BuildSettingsVersion.V7; ExtraModuleNames.Add("CrossplayHarness"); } }\n',
        'Source/CrossplayHarnessEditor.Target.cs': 'using UnrealBuildTool;\npublic class CrossplayHarnessEditorTarget : TargetRules { public CrossplayHarnessEditorTarget(TargetInfo T):base(T) { Type=TargetType.Editor; DefaultBuildSettings=BuildSettingsVersion.V7; ExtraModuleNames.Add("CrossplayHarness"); } }\n',
        'Source/CrossplayHarness/CrossplayHarness.Build.cs': 'using UnrealBuildTool;\npublic class CrossplayHarness : ModuleRules { public CrossplayHarness(ReadOnlyTargetRules T):base(T) { PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs; PublicDependencyModuleNames.AddRange(new[]{"Core","CoreUObject","Engine"}); } }\n',
        'Source/CrossplayHarness/CrossplayHarness.cpp': '#include "Modules/ModuleManager.h"\nIMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl,CrossplayHarness,"CrossplayHarness");\n',
        'Config/DefaultEngine.ini': '[OnlineSubsystem]\nDefaultPlatformService=NULL\n\n[OnlineSubsystemEOS]\nbEnabled=false\n\n[OnlineSubsystemSteam]\nbEnabled=false\n\n[EOSSDK]\nbHasProjectBinary=true\n\n[/Script/UniversalCrossplay.CrossplaySettings]\nbEnableOnline=false\n',
    }
    for relative, content in files.items():
        dest = root / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(content, encoding='utf-8')
    return root / 'CrossplayHarness.uproject'

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    try:
        print(create(args.output))
    except (OSError, ValueError) as error:
        raise SystemExit(str(error))
