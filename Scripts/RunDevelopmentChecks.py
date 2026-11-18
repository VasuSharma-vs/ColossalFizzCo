"""Run project-local native tests and optional local-IP multiplayer checks, without Epic sign-in."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys,time

def sha(data):return hashlib.sha256(data).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--suite',choices=['game','plugin','network','all'],default='all')
    parser.add_argument('--project',help='Defaults to this script\'s ColossalFizzCo project.')
    parser.add_argument('--editor',help='Override UnrealEditor-Cmd.exe path.')
    args=parser.parse_args()
    project=Path(args.project).resolve() if args.project else Path(__file__).resolve().parents[1]/'ColossalFizzCo.uproject'
    root=project.parent
    config=json.loads(project.read_text(encoding='utf-8-sig'))
    version=config.get('EngineAssociation','5.8')
    engine=Path(os.environ.get('UE_ENGINE_ROOT','C:/Program Files/Epic Games/UE_'+version))
    editor=Path(args.editor) if args.editor else engine/'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    if not editor.is_file():raise RuntimeError('UnrealEditor-Cmd.exe not found; use --editor.')
    processes=subprocess.run(['powershell','-NoProfile','-Command',
        "@(Get-Process UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue).Count"],capture_output=True,text=True)
    if processes.stdout.strip()!='0':raise RuntimeError('Close Unreal before running development checks.')
    directory=root/'Saved/DevelopmentChecks'/time.strftime('%Y%m%d_%H%M%S')
    directory.mkdir(parents=True,exist_ok=False)
    saves={p:p.read_bytes() for p in (root/'Saved/SaveGames').glob('*.sav')}
    results={};restored=[]
    try:
        for prefix,label,minimum in [('ColossalFizzCo','game',72),('UniversalCrossplay','plugin',5)]:
            if args.suite not in [label,'all']:continue
            report=directory/label/'Report';log=directory/(label+'.log')
            cmd=[str(editor),str(project),'/Engine/Maps/Entry','-unattended','-nullrhi','-nosound','-nop4','-NoLiveCoding',
                 '-DisablePlugins=EditorToolset,ModelContextProtocol,MCPClientToolset',
                 '-UserDir='+str(directory/(label+'User')),'-abslog='+str(log),
                 '-ExecCmds=Automation RunTests '+prefix,'-TestExit=Automation Test Queue Empty',
                 '-ReportExportPath='+str(report),'-stdout']
            with (directory/(label+'_stdout.log')).open('w',encoding='utf-8') as stream:
                process=subprocess.run(cmd,stdout=stream,stderr=subprocess.STDOUT,timeout=1200,cwd=root)
            data=json.loads((report/'index.json').read_text(encoding='utf-8-sig')) if (report/'index.json').is_file() else {}
            result={k:data.get(k) for k in ['succeeded','succeededWithWarnings','failed','notRun']}
            performed=data.get('succeeded',0)+data.get('succeededWithWarnings',0)
            result.update(exit_code=process.returncode,accepted=process.returncode==0 and performed>=minimum
                          and data.get('failed',1)==0 and data.get('notRun',1)==0)
            results[label]=result
            print(label+': '+json.dumps(result),flush=True)
            if not result['accepted']:raise RuntimeError(label+' checks failed; inspect '+str(directory))
        if args.suite in ['network','all']:
            runner=root/'Saved/ProductCatalog_20261115/RunNetwork.py'
            if not runner.is_file():raise RuntimeError('Relocated local network runner is missing.')
            with (directory/'network_stdout.log').open('w',encoding='utf-8') as stream:
                process=subprocess.run([sys.executable,str(runner)],stdout=stream,stderr=subprocess.STDOUT,timeout=360,cwd=root)
            result=json.loads((runner.parent/'NetworkResult.json').read_text())
            result['runner_exit_code']=process.returncode
            results['network']=result
            print('network: '+json.dumps(result),flush=True)
            if process.returncode or not result.get('accepted'):raise RuntimeError('Network checks failed.')
    except Exception as ex:
        results['error']=str(ex)
    finally:
        # Only these owned unattended tests run while the user's editor is closed.
        for path,original in saves.items():
            if not path.exists() or sha(path.read_bytes())!=sha(original):
                path.write_bytes(original);restored.append(path.name)
    accepted='error' not in results and bool(results) and all(r.get('accepted') for r in results.values())
    result={'accepted':accepted,'project':str(project),'directory':str(directory),'results':results,
            'original_saves_preserved':all(p.exists() and sha(p.read_bytes())==sha(v) for p,v in saves.items()),
            'restored_test_modified_saves':restored}
    (directory/'Result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    (root/'Saved/DevelopmentChecks/Latest.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps({'accepted':accepted,'directory':str(directory),'original_saves_preserved':result['original_saves_preserved']}))
    return 0 if accepted and result['original_saves_preserved'] else 1

if __name__=='__main__':
    try:sys.exit(main())
    except (RuntimeError,OSError,ValueError) as ex:raise SystemExit(str(ex))
