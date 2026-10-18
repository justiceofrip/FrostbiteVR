"""Stage a portable public runtime from an exact frozen normal-build receipt.

No build, game launch, injection or network calls. The allowlist is explicit;
game-derived body/audio caches and diagnostic outputs are never packaged.
"""
import argparse
import ast
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil

LOADER_SHA='a69729a3348bc4dcb9be126a49d594074e477be7312f65180b0d082bf100fa5f'
GAME_SHA='3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'
AUDIO_GUARD="    if output.drive.upper()!='G:' or output==Path(__file__).resolve().parents[1] or Path(__file__).resolve().parents[1] in output.parents:\n        raise ValueError('Private audio cache must be outside the source repository on G:')"
SHELL_GUARD="    if output.drive.upper() != 'G:' or output == source or source in output.parents:\n        raise ValueError('Private audio cache must be outside the source repository on G:')"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def local_imports(root, seeds):
    pending=list(seeds); seen=set()
    while pending:
        name=pending.pop()
        if name in seen: continue
        seen.add(name)
        tree=ast.parse((root/(name+'.py')).read_text(encoding='utf-8-sig'))
        for node in ast.walk(tree):
            names=[x.name for x in node.names] if isinstance(node,ast.Import) else [node.module] if isinstance(node,ast.ImportFrom) and node.module else []
            pending.extend(x for x in names if (root/(x+'.py')).is_file())
    return sorted(seen)


def public_audio_tool(name, text):
    # Preserve the already-tested resource validation/PCM/cue bytes verbatim.
    # Only private workspace placement policy differs in the public copy.
    if name=='bc2_mechanism_audio_cache':
        before=AUDIO_GUARD
        after='    from prepare_runtime_assets import validate_cache_path\n    validate_cache_path(output, Path(__file__).resolve().parents[2])'
    elif name=='bc2_shell_reload_audio_cache':
        before=SHELL_GUARD
        after='    from prepare_runtime_assets import validate_cache_path\n    validate_cache_path(output, source.parent)'
    else:return text
    if text.count(before)!=1: raise ValueError('Reviewed private audio guard changed')
    return text.replace(before,after)


def safe_relative(value):
    path=PurePosixPath(value)
    if path.is_absolute() or any(x in ('','..','.') for x in value.split('/')) or '\\' in value or ':' in value:
        raise ValueError('Unsafe staging path')
    return path


def stage(repo, receipt_path, expected_receipt_sha, output, version, dependencies=None):
    if sha(receipt_path)!=expected_receipt_sha: raise ValueError('Frozen build receipt changed')
    receipt=json.loads(receipt_path.read_text(encoding='utf-8-sig'))
    if not receipt.get('full_normal_builds') or receipt.get('diagnostic_only') or receipt.get('diagnostic_kind')!='normal':
        raise ValueError('Only a full normal build can be packaged')
    if any(receipt.get(name) for name in receipt if name.endswith('_probe')):
        raise ValueError('Diagnostic fixture flag in normal receipt')
    if output.exists(): raise ValueError('Preserve existing staged runtime; use a new output folder')
    output.mkdir(parents=True)
    files=[]; transforms=[]
    def copy(source,name):
        safe_relative(name)
        if source.is_symlink(): raise ValueError('Linked input refused')
        target=output/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target);files.append(name)
    for key in ('probe','trace','host'):
        entry=receipt['binaries'][key]; source=Path(entry['path'])
        if sha(source)!=entry['sha256']: raise ValueError('Frozen binary changed: '+key)
        copy(source,'bin/'+source.name)
    loader=repo/'build/x64/runtime/openxr/win64/openxr_loader.dll'
    if sha(loader)!=LOADER_SHA: raise ValueError('OpenXR loader changed')
    copy(loader,'bin/openxr_loader.dll')
    for name in ('Entry.ps1','Runtime.Core.ps1','Dependencies.ps1','Session.ps1'):
        copy(repo/'launcher/runtime'/name,name)
    for name in ('prepare_runtime_assets.py','wait_main_menu.py','activate_game_once.py','runtime_process.py'):
        copy(repo/'launcher/runtime'/name,'tools/'+name)
    copy(repo/'tools/read_bc2.py','tools/read_bc2.py')
    for name in local_imports(repo/'tools',('bc2_body_ammo_assets','bc2_mechanism_audio_cache','bc2_shell_reload_audio_cache')):
        source=repo/'tools'/(name+'.py'); target='tools/'+name+'.py'
        text=source.read_text(encoding='utf-8-sig'); public=public_audio_tool(name,text)
        if public==text:copy(source,target)
        else:
            (output/target).write_text(public,encoding='utf-8',newline='\n');files.append(target)
            transforms.append({'path':target,'original_sha256':sha(source),'packaged_sha256':sha(output/target),'change':'output placement validation only; PCM/asset logic unchanged'})
    copy(repo/'config/body-ammo-assets.json','config/body-ammo-assets.json')
    copy(repo/'config/ffmpeg-download.json','config/ffmpeg-download.json')
    for name in ('BFVR-MIT.txt','MinHook-BSD.txt','OpenXR-Apache-2.0.txt','JsonCpp-MIT.txt','LSLib-MIT.txt'):
        copy(repo/'licenses'/name,'licenses/'+name)
    copy(repo/'launcher/runtime/THIRD_PARTY_NOTICES.md','THIRD_PARTY_NOTICES.md')
    if dependencies:
        deps=json.loads(dependencies.read_text(encoding='utf-8-sig'))
        for entry in deps['files']:
            name=entry['path'];safe_relative(name)
            if not name.startswith('python/'): raise ValueError('Only Python is bundled; FFmpeg must be fetched upstream during local preparation')
            source=Path(entry['source'])
            if sha(source)!=entry['sha256']: raise ValueError('Portable dependency hash changed')
            if name in files: raise ValueError('Duplicate dependency path')
            copy(source,name)
    recipe_files=[name for name in files if name=='Dependencies.ps1' or name.startswith(('tools/','config/')) or
                  (name.startswith('python/') and Path(name).suffix.lower() in ('.exe','.dll','.pyd','.zip','._pth'))]
    recipe=hashlib.sha256('\n'.join(name+':'+sha(output/name) for name in sorted(recipe_files)).encode()).hexdigest()
    config={'schema':'bc2vr.runtime.v1','version':version,'checkpoint':receipt['checkpoint'],
            'source_sha256':receipt['source_sha256'],'frozen_receipt_sha256':expected_receipt_sha,
            'diagnostic_only':False,'game_sha256':[GAME_SHA],'asset_recipe_sha256':recipe,
            'files':[{'path':name,'sha256':sha(output/name)} for name in sorted(files)],'packaged_source_adaptations':transforms,
            'start_at':'MainMenu','requires_owned_game':True,'game_assets_distributed':False,'headset_accepted':False}
    (output/'runtime.json').write_text(json.dumps(config,indent=2)+'\n',encoding='utf-8')
    files.append('runtime.json')
    (output.parent/(output.name+'-files.json')).write_text(json.dumps(sorted(files),indent=2)+'\n',encoding='utf-8')
    return {'output':str(output),'runtime_files':str(output.parent/(output.name+'-files.json')),
            'files':len(files),'bytes':sum((output/name).stat().st_size for name in files),'source_sha256':receipt['source_sha256'],
            'bundled_python':(output/'python/python.exe').is_file(),'bundled_ffmpeg':(output/'ffmpeg/ffmpeg.exe').is_file()}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('repo','receipt','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--receipt-sha256',required=True);p.add_argument('--version',default='0.1.4');p.add_argument('--dependencies',type=Path)
    a=p.parse_args();print(json.dumps(stage(a.repo,a.receipt,a.receipt_sha256,a.output,a.version,a.dependencies),indent=2))


if __name__=='__main__':main()
