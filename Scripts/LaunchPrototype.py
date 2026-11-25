"""Launch the Editor-based prototype. Credentials are read only by Unreal."""
from pathlib import Path
import argparse,json,os,subprocess,sys

def prepare(project,online=True,low=False):
    root=Path(project).resolve().parent
    source=root/'Config/Custom/Epic/DefaultEngine.ini'
    if online and not source.is_file():raise ValueError('Epic profile is missing. Use --offline or complete the multiplayer setup.')
    profile='EpicPrototype' if online else 'PrototypeOffline'
    target=root/'Config/Custom'/profile/'DefaultEngine.ini'
    text=source.read_text(encoding='utf-8-sig') if online else '[OnlineSubsystem]\nDefaultPlatformService=NULL\n[/Script/UniversalCrossplay.CrossplaySettings]\nbEnableOnline=false\n'
    text+='\n[/Script/OnlineSubsystemEOS.EOSSettings]\nbEnableOverlay=false\nbEnableSocialOverlay=false\nbPreferPersistentAuth=false\n'
    text+='\n[/Script/UniversalCrossplay.CrossplaySettings]\nOperationTimeout=180\n'
    if low:
        text+='\n[/Script/Engine.RendererSettings]\nr.RayTracing=False\nr.PathTracing=False\nr.Shadow.Virtual.Enable=0\nr.DynamicGlobalIlluminationMethod=0\nr.ReflectionMethod=0\n'
    ignore=root/'.gitignore';old=ignore.read_text(encoding='utf-8-sig') if ignore.exists() else ''
    pattern='/Config/Custom/'+profile+'/'
    if pattern not in old.splitlines():ignore.write_text(old+'\n'+pattern+'\n',encoding='utf-8')
    target.parent.mkdir(parents=True,exist_ok=True)
    pending=target.with_suffix('.pending');pending.write_text(text,encoding='utf-8');os.replace(pending,target)
    return profile

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--project');p.add_argument('--editor');p.add_argument('--offline',action='store_true')
    p.add_argument('--quality',choices=('low','project'),default='project',
                   help='Use saved graphics by default or force a low-resource launch.')
    p.add_argument('--user-dir',help='Optional separate data directory for testing.')
    p.add_argument('--dry-run',action='store_true');a=p.parse_args()
    project=Path(a.project).resolve() if a.project else Path(__file__).resolve().parents[1]/'ColossalFizzCo.uproject'
    if not project.is_file():raise ValueError('Project file was not found.')
    version=json.loads(project.read_text(encoding='utf-8-sig')).get('EngineAssociation','5.8')
    engine=Path(os.environ.get('UE_ENGINE_ROOT','C:/Program Files/Epic Games/UE_'+version))
    editor=Path(a.editor) if a.editor else engine/'Engine/Binaries/Win64/UnrealEditor.exe'
    if not editor.is_file():raise ValueError('Unreal Editor was not found. Supply --editor with your installed executable.')
    profile=prepare(project,not a.offline,a.quality=='low')
    command=[str(editor),str(project),'/Game/NormalColaPrototype/Lvl_PrototypeMainMenu','-game',
             '-CustomConfig='+profile,'-NoLiveCoding','-nop4']
    if a.quality=='low':
        command+=['-windowed','-ResX=1280','-ResY=720','-noraytracing',
                  '-ExecCmds=t.MaxFPS 30,r.Streaming.PoolSize 256,sg.ViewDistanceQuality 0,sg.ShadowQuality 0,sg.PostProcessQuality 0,sg.EffectsQuality 0,sg.FoliageQuality 0,sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0']
    if a.user_dir:command+=['-UserDir='+str(Path(a.user_dir).resolve())]
    if a.dry_run:print('Prototype launch configuration validated. '+profile+'; '+a.quality+' graphics. No credentials printed.');return
    subprocess.Popen(command,cwd=project.parent,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    print('Opened Colossal Fizz Co prototype. Epic authentication is completed by the player.')
if __name__=='__main__':
    try:main()
    except (ValueError,OSError,json.JSONDecodeError) as ex:raise SystemExit(str(ex))
