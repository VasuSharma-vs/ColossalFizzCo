from pathlib import Path
import argparse,json,re,os

def configure(project_file,private_config,overwrite=False,store='Both'):
 if store not in ('Both','Steam','Epic'):raise ValueError('Choose Both, Steam or Epic.')
 stores=('Steam','Epic') if store=='Both' else (store,)
 project=Path(project_file).resolve();root=project.parent
 if project.suffix.lower()!='.uproject' or not project.is_file():raise ValueError('Provide an existing .uproject file.')
 d=json.loads(Path(private_config).read_text(encoding='utf-8-sig'))
 if not isinstance(d,dict):raise ValueError('The private configuration must be a JSON object.')
 if any(k.lower() in {'steampublisherkey','publisherkey','rtcadminsecret'} for k in d):raise ValueError('Publisher/admin secrets belong in service-side configuration, not game profiles.')
 fields=['ArtifactName','ProductId','SandboxId','DeploymentId','EncryptionKey','CompatibilityId','LobbyMap','GameplayMap','ReturnMap']
 for selected in stores:fields.extend((selected+'ClientId',selected+'ClientSecret'))
 if 'Steam' in stores:fields.append('SteamTokenIdentity')
 for k in fields:
  v=d.get(k)
  if not isinstance(v,str) or not v or any(c in v for c in '\r\n"(),;'):raise ValueError('Missing or invalid field: '+k)
 if not re.fullmatch(r'[0-9a-fA-F]{64}',d['EncryptionKey']):raise ValueError('EncryptionKey must contain 64 hexadecimal characters.')
 if 'Steam' in stores and (type(d.get('SteamAppId')) is not int or not 0<d['SteamAppId']<=0xffffffff):raise ValueError('A valid SteamAppId is required for a Steam profile.')
 for k in ('ProductId','SandboxId','DeploymentId'):
  if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}',d[k]):raise ValueError('Invalid portal identifier for '+k)
 for k in ('LobbyMap','GameplayMap','ReturnMap'):
  v=d[k]
  if not v.startswith(('/Game/','/Engine/')) or '..' in v or not re.fullmatch(r'/[A-Za-z0-9_/.]+',v):raise ValueError('Invalid cooked map path: '+k)
  if '.' not in v:d[k]=v+'.'+v.rsplit('/',1)[-1]
 targets=[]
 for store in stores:
  p=root/'Config/Custom'/store/'DefaultEngine.ini'
  if not p.resolve().is_relative_to(root):raise ValueError('Profile path escaped the project.')
  if p.exists() and not overwrite:raise ValueError('Profile already exists; inspect it before using --overwrite.')
  steam=store=='Steam';b=lambda x:'true' if x else 'false'
  text='[OnlineSubsystem]\nDefaultPlatformService=EOS\nNativePlatformService='+('Steam' if steam else 'NULL')+'\n\n[OnlineSubsystemEOS]\nbEnabled=true\n\n[OnlineSubsystemSteam]\nbEnabled='+b(steam)+'\nbUseSteamNetworking=false\nbInitServerOnClient=false\n'
  if steam:text+='SteamDevAppId='+str(d['SteamAppId'])+'\nSteamAppId='+str(d['SteamAppId'])+'\n'
  text+='\n[EOSSDK]\nbHasProjectBinary=true\n\n[/Script/OnlineSubsystemEOS.EOSSettings]\nbUseEOSConnect=true\nbUseEAS='+b(not steam)+'\nbUseEOSRTC=true\nbEnableOverlay='+b(not steam)+'\nbEnableSocialOverlay='+b(not steam)+'\nbEnableEditorOverlay=false\n'
  if steam:text+='SteamTokenType=WebApi:'+d['SteamTokenIdentity']+'\n'
  text+='DefaultArtifactName='+d['ArtifactName']+'\n'
  text+='!Artifacts=ClearArray\n+Artifacts=(ArtifactName="'+d['ArtifactName']+'",ClientId="'+d[store+'ClientId']+'",ClientSecret="'+d[store+'ClientSecret']+'",ProductId="'+d['ProductId']+'",SandboxId="'+d['SandboxId']+'",DeploymentId="'+d['DeploymentId']+'",ClientEncryptionKey="'+d['EncryptionKey']+'")\n'
  text+='\n[/Script/UniversalCrossplay.CrossplaySettings]\nbEnableOnline=true\nbEnableVoice=true\nbMicrophoneMutedByDefault=true\nCapacity=2\nCompatibilityId='+d['CompatibilityId']+'\n'
  for k in ('LobbyMap','GameplayMap','ReturnMap'):text+=k+'='+d[k]+'\n'
  targets.append((p,text))
 ignore=root/'.gitignore';old=ignore.read_text(encoding='utf-8-sig') if ignore.exists() else ''
 for selected in stores:
  pattern='/Config/Custom/'+selected+'/DefaultEngine.ini'
  if pattern not in old:old+='\n'+pattern+'\n'
 temp=ignore.with_suffix('.pending');temp.write_text(old,encoding='utf-8');os.replace(temp,ignore)
 for p,text in targets:
  p.parent.mkdir(parents=True,exist_ok=True);temp=p.with_suffix('.pending');temp.write_text(text,encoding='utf-8');os.replace(temp,p)
 return [str(p) for p,_ in targets]
if __name__=='__main__':
 p=argparse.ArgumentParser(description='Write private local store profiles. Does not contact or publish to services.')
 p.add_argument('--project',required=True);p.add_argument('--config',required=True);p.add_argument('--overwrite',action='store_true');p.add_argument('--store',choices=('Both','Steam','Epic'),default='Both');a=p.parse_args()
 try:
  paths=configure(a.project,a.config,a.overwrite,a.store)
 except (ValueError,OSError,json.JSONDecodeError) as ex:
  # Field names and paths only; never echo the input JSON or credential values.
  raise SystemExit(str(ex))
 print('Created the selected private profiles. Credential values are not printed. Restart with the chosen development CustomConfig profile.')
 for path in paths:print(path)
