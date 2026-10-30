"""Build private runtime assets from the selected legally installed game.

Only derived output goes into the explicitly chosen local-drive cache. This
module performs no process access, downloads, archive writes or game launch.
"""
import argparse
import ctypes
import hashlib
import json
import re
import stat
from datetime import datetime, timezone
from pathlib import Path
from bc2_body_ammo_assets import main as body_main
from bc2_mechanism_audio_cache import prepare as pump_prepare
from bc2_shell_reload_audio_cache import prepare as shell_prepare


def drive_type(root):
    get_drive_type = ctypes.windll.kernel32.GetDriveTypeW
    get_drive_type.argtypes = [ctypes.c_wchar_p]
    get_drive_type.restype = ctypes.c_uint
    return get_drive_type(str(root))


def validate_cache_path(output, package):
    if not output.is_absolute() or len(output.drive) != 2:
        raise ValueError('Choose an absolute cache folder on a local drive')
    if drive_type(output.anchor) != 3:  # DRIVE_FIXED; C: is allowed like any other fixed drive.
        raise ValueError('Choose a cache folder on a ready local fixed drive')
    if output == package or package in output.parents:
        raise ValueError('Cache must be outside the versioned application package')
    for parent in (output, *output.parents):
        if parent.is_symlink() or (hasattr(parent, 'is_junction') and parent.is_junction()):
            raise ValueError('Linked cache paths are not supported')
        # Python 3.11 has no Path.is_junction(). Reject every reparse tag,
        # including a junction whose destination would otherwise hide a link.
        if hasattr(parent, 'lstat'):
            try:
                info = parent.lstat()
            except FileNotFoundError:
                continue
            except NotADirectoryError as exc:
                raise ValueError('A cache path component is a file; choose a fresh launcher folder') from exc
            if getattr(info, 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
                raise ValueError('Linked cache paths are not supported')


def validate_managed_cache(output, managed_root, game, package, recipe):
    """Authorize only this installed launcher's recipe cache, before any write."""
    for path in (output, managed_root):
        validate_cache_path(path, package)
        if path.exists() and not path.is_dir():
            raise ValueError('A managed cache directory is occupied by a file; choose a fresh launcher folder')
        if '..' in path.parts:
            raise ValueError('Use the launcher-managed cache path without parent traversal')
    output, managed_root, package = output.resolve(), managed_root.resolve(), package.resolve()
    game = game.resolve()
    for path in (output, managed_root):
        validate_cache_path(path, package)
    if package.parent.name.casefold() != 'versions' or not re.fullmatch(r'\d{1,5}\.\d{1,5}\.\d{1,5}', package.name):
        raise ValueError('Run asset preparation through the installed BC2 VR launcher')
    launcher = package.parent.parent
    expected = launcher / 'data' / 'runtime' / 'cache'
    if managed_root != expected or not re.fullmatch(r'[a-f0-9]{64}', recipe) or output != expected / recipe:
        raise ValueError('Use this launcher\'s data/runtime/cache/<recipe> folder for generated assets')
    marker = package / 'package.json'
    # The Worker verifies the package contents. This marker additionally binds
    # the cache exception to the installed layout, not a caller-supplied root.
    if marker.is_symlink() or (hasattr(marker, 'is_junction') and marker.is_junction()):
        raise ValueError('Linked launcher package markers are not supported')
    try:
        info = marker.lstat()
        if (getattr(info, 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT or
                not stat.S_ISREG(info.st_mode) or info.st_size > 1024 * 1024):
            raise ValueError('Invalid installed launcher package marker')
        identity = json.loads(marker.read_text(encoding='utf-8-sig'))
    except (OSError, ValueError) as exc:
        raise ValueError('Run asset preparation through a verified installed BC2 VR launcher') from exc
    if (not isinstance(identity, dict) or identity.get('schema') != 'fvr.desktop.package.v1' or
            identity.get('version') != package.name or identity.get('runtime_entry') != 'runtime/Entry.ps1'):
        raise ValueError('Run asset preparation through a verified installed BC2 VR launcher')
    if launcher == game or (game in launcher.parents and
                            launcher.relative_to(game).parts[0].casefold() in ('dist', 'package')):
        raise ValueError('Choose a dedicated BC2VR-Launcher folder, outside the game root itself and its Dist/Package folders')
    if output == game or game in output.parents:
        if game not in launcher.parents:
            raise ValueError('Generated assets must stay in the dedicated launcher-owned cache')
    # Validate every path the three generators may write. An existing linked
    # leaf could otherwise redirect a safe cache directory into stock assets.
    directories = ('pump', 'shell')
    files = ('assets.json', 'assets.json.tmp', 'preparation.json', 'preparation.json.tmp',
             'body-ammo-assets.fvrprop', 'pump/spas-rear.wav', 'pump/spas-closure.wav',
             'pump/mechanism-audio.json', 'shell/spas-shell-insert.wav', 'shell/shell-reload-audio.json')
    for relative in (*directories, *files):
        path = output / relative
        validate_cache_path(path, package)
        try:
            info = path.lstat()
        except FileNotFoundError:
            continue
        if relative in directories:
            if not stat.S_ISDIR(info.st_mode):
                raise ValueError('A managed cache directory is occupied by a file; choose a fresh launcher folder')
        elif not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
            raise ValueError('A managed cache output is linked or not a regular file; choose a fresh launcher folder')
    return output


def prepare_assets(args, runtime):
    """Publish stage evidence even when a selected installation cannot be read.

    Only complete, verified output publishes assets.json. The status is
    diagnostic only: the launcher requires both a clean process exit and its
    own content verification before admitting the final cache.
    """
    output = args.output.resolve()
    report = {'schema': 'bc2vr.asset-preparation.v1', 'recipe_sha256': args.recipe,
              'game_sha256': args.game_sha256, 'state': 'running', 'stage': 'game fingerprint',
              'error': None, 'game_started': False}

    def publish(stage, state='running', error=None):
        report.update(stage=stage, state=state, error=error,
                      utc=datetime.now(timezone.utc).isoformat())
        temporary = output / 'preparation.json.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        temporary.replace(output / 'preparation.json')

    output.mkdir(parents=True, exist_ok=True)
    # Discard only the old completion marker: a failed regeneration must not
    # leave an earlier success receipt authorizing a partial cache.
    (output / 'assets.json').unlink(missing_ok=True)
    try:
        publish('game fingerprint')
        game = args.game.resolve()
        if hashlib.sha256((game/'BFBC2Game.exe').read_bytes()).hexdigest() != args.game_sha256:
            raise ValueError('Game executable changed')
        publish('body equipment')
        body_main(['--game',str(game),'--output',str(output/'body-ammo-assets.fvrprop'),
                   '--body-equipment','--resource-content','--catalog',str(runtime/'config/body-ammo-assets.json')])
        publish('pump audio')
        pump_prepare(game,args.ffmpeg,output/'pump')
        publish('shell reload audio')
        shell_prepare(game,args.ffmpeg,output/'shell')
        publish('cache verification')
        names=('body-ammo-assets.fvrprop','pump/spas-rear.wav','pump/spas-closure.wav',
               'pump/mechanism-audio.json','shell/spas-shell-insert.wav','shell/shell-reload-audio.json')
        files=[{'path':name,'sha256':hashlib.sha256((output/name).read_bytes()).hexdigest(),
                'bytes':(output/name).stat().st_size} for name in names]
        if sum(row['bytes'] for row in files)>64*1024*1024:
            raise ValueError('Derived runtime asset size exceeded 64 MiB')
        receipt={'schema':'bc2vr.local-assets.v1','recipe_sha256':args.recipe,
                 'game_sha256':args.game_sha256,'files':files,'redistributable':False}
        temporary=output/'assets.json.tmp'
        temporary.write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
        temporary.replace(output/'assets.json')
        publish('complete', 'complete')
        print(json.dumps({'prepared':True,'bytes':sum(row['bytes'] for row in files),'game_started':False}))
    except Exception as exc:
        # Keep errors bounded and single-line for the launcher status panel.
        detail = (type(exc).__name__ + ': ' + str(exc)).replace('\r', ' ').replace('\n', ' ')[:1200]
        publish(report['stage'], 'failed', detail)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('game', 'output', 'ffmpeg', 'managed-cache-root'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--recipe', required=True)
    parser.add_argument('--game-sha256', required=True)
    args = parser.parse_args()
    runtime=Path(__file__).resolve().parents[1]
    validate_cache_path(args.output, runtime.parent)
    output=args.output.resolve()
    # Check containment again after collapsing '..', without hiding a linked
    # ancestor from the first check above.
    validate_cache_path(output, runtime.parent)
    validate_managed_cache(args.output, args.managed_cache_root, args.game, runtime.parent, args.recipe)
    prepare_assets(args, runtime)


if __name__=='__main__':
    main()
