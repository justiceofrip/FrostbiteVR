"""Bounded exact resource reads across explicitly supplied authored dependencies.

Campaign weapons can reference the shared skeleton without owning a duplicate
payload. Archive order never resolves conflicting resource bytes or types.
"""
from pathlib import Path
from inspect_bc2_mesh_asset import Archive, sha


def resource_key(name):
    if not isinstance(name, str) or not name or '\\' in name or ':' in name or any(p in ('', '.', '..') for p in name.split('/')):
        raise ValueError('Exact archive-relative resource required')
    return name.casefold()


class AuthoredArchiveSet:
    def __init__(self, sources):
        if not 0 < len(sources) <= 32:
            raise ValueError('Authored dependency archive count bound')
        self.sources = list(sources)
        self.entries = {}
        seen = set()
        for relative, archive in self.sources:
            key = resource_key(relative)
            if key in seen:
                raise ValueError('Duplicate authored dependency archive')
            seen.add(key)
            local = set()
            for entry in archive.entries:
                if entry.flags != 65536:
                    continue
                key = resource_key(entry.name)
                if key in local:
                    raise ValueError('Ambiguous case-insensitive archive resource')
                local.add(key)
                self.entries.setdefault(key, []).append((relative, archive, entry))

    @classmethod
    def open(cls, game, relatives, *, archive_type=Archive):
        game = game.resolve()
        if not 0 < len(relatives) <= 32:
            raise ValueError('Authored dependency archive count bound')
        sources = []
        for relative in relatives:
            name = Path(relative).as_posix()
            resource_key(name)
            path = (game / name).resolve()
            if not path.is_relative_to(game):
                raise ValueError('Authored dependency escapes game root')
            sources.append((name, archive_type(path)))
        return cls(sources)

    def read(self, names):
        wanted = {resource_key(name) for name in names}
        if len(wanted) > 4096:
            raise ValueError('Authored resource closure bound')
        output = {}
        for relative, archive in self.sources:
            selected = [entry for key in sorted(wanted) for source, owner, entry in self.entries.get(key, ()) if owner is archive and source == relative]
            blobs = archive.read_selected([entry.name for entry in selected]) if selected else {}
            for entry in selected:
                key = resource_key(entry.name)
                data = blobs[entry.name]
                old = output.get(key)
                if old and (old[1]['kind'] != entry.kind or old[0] != data):
                    raise ValueError('Conflicting authored dependency resource: ' + entry.name)
                if old is None:
                    output[key] = (data, dict(archive=relative, archive_index_sha256=archive.index_sha256,
                                             kind=entry.kind, resource=entry.name, sha256=sha(data)))
        return output
