"""Build private runtime assets from the selected legally installed game.

Only derived output goes into the explicitly chosen data-drive cache. This
module performs no process access, downloads, archive writes or game launch.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
from bc2_body_ammo_assets import main as body_main
from bc2_mechanism_audio_cache import prepare as pump_prepare
from bc2_shell_reload_audio_cache import prepare as shell_prepare


def validate_cache_path(output, package):
    if not output.is_absolute() or len(output.drive) != 2 or output.drive.upper() == os.environ.get('SystemDrive', 'C:').upper():
        raise ValueError('Choose a local data-drive cache outside the Windows drive')
    if output == package or package in output.parents:
        raise ValueError('Cache must be outside the versioned application package')
    for parent in (output, *output.parents):
        if parent.is_symlink() or (hasattr(parent, 'is_junction') and parent.is_junction()):
            raise ValueError('Linked cache paths are not supported')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('game', 'output', 'ffmpeg'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--recipe', required=True)
    parser.add_argument('--game-sha256', required=True)
    args = parser.parse_args()
    output=args.output.resolve(); runtime=Path(__file__).resolve().parents[1]
    validate_cache_path(output, runtime.parent)
    game=args.game.resolve()
    if output == game or game in output.parents:
        raise ValueError('Generated assets must not be written into the game installation')
    if hashlib.sha256((game/'BFBC2Game.exe').read_bytes()).hexdigest() != args.game_sha256:
        raise ValueError('Game executable changed')
    output.mkdir(parents=True, exist_ok=True)
    body_main(['--game',str(game),'--output',str(output/'body-ammo-assets.fvrprop'),
               '--body-equipment','--catalog',str(runtime/'config/body-ammo-assets.json')])
    pump_prepare(game,args.ffmpeg,output/'pump')
    shell_prepare(game,args.ffmpeg,output/'shell')
    names=('body-ammo-assets.fvrprop','pump/spas-rear.wav','pump/spas-closure.wav',
           'pump/mechanism-audio.json','shell/spas-shell-insert.wav','shell/shell-reload-audio.json')
    files=[{'path':name,'sha256':hashlib.sha256((output/name).read_bytes()).hexdigest(),
            'bytes':(output/name).stat().st_size} for name in names]
    if sum(row['bytes'] for row in files)>32*1024*1024:
        raise ValueError('Derived runtime asset size exceeded 32 MiB')
    receipt={'schema':'bc2vr.local-assets.v1','recipe_sha256':args.recipe,
             'game_sha256':args.game_sha256,'files':files,'redistributable':False}
    temporary=output/'assets.json.tmp'
    temporary.write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    temporary.replace(output/'assets.json')
    print(json.dumps({'prepared':True,'bytes':sum(row['bytes'] for row in files),'game_started':False}))


if __name__=='__main__':
    main()
