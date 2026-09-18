"""Verify release ZIP payloads and manifests before upload."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile

p = argparse.ArgumentParser()
p.add_argument('--directory', type=Path, required=True)
p.add_argument('--version', required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
results = []
archives = sorted(a.directory.glob('*.zip'))
assert len(archives) == 3, 'Expected portable and two corresponding-source ZIPs'
for archive in archives:
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    assert archive.with_suffix('.zip.sha256').read_text().split()[0].lower() == digest
    with zipfile.ZipFile(archive) as z:
        names = [i.filename for i in z.infolist() if not i.is_dir()]
        assert len(names) == len(set(names))
        for name in names:
            path = PurePosixPath(name)
            assert not path.is_absolute() and '..' not in path.parts and '\\' not in name
            assert '.git' not in path.parts and 'third_party_local/nvidia' not in name.lower()
        if 'portable' in archive.name:
            prefix = f'Veyra-{a.version}-win64-portable/'
            manifest = json.loads(z.read(prefix + 'package-manifest.json').decode('utf-8-sig'))
            assert manifest['version'] == a.version
            entries = manifest['files']
            assert set(names) == {prefix + e['path'] for e in entries} | {prefix + 'package-manifest.json'}
            for e in entries:
                data = z.read(prefix + e['path'])
                assert len(data) == e['size'] and hashlib.sha256(data).hexdigest() == e['sha256'].lower()
                assert PurePosixPath(e['path']).suffix.lower() not in {'.h', '.hpp', '.cpp', '.c', '.obj', '.lib', '.pdb', '.log', '.mp4', '.zip', '.onnx', '.pth', '.addon64'}
                assert PurePosixPath(e['path']).name not in {'last-applied.v1', 'ui-preferences.v1', 'presets.v1', 'veyra.ini'}
            runtime = []
            for folder in ['runtime/experimental/', 'runtime_local/intel/experimental/',
                           'runtime_local/amd/fidelityfx/']:
                runtime += json.loads(z.read(prefix + folder + 'release-runtime-manifest.json').decode('utf-8-sig'))['files']
            assert len(runtime) == 11 and sum(e['authenticode'] == 'HashMismatch' for e in runtime) == 2
            for notice in ['RTX40MFG_LICENSE.txt', 'HDE_LICENSE.txt']:
                assert prefix + 'licenses/' + notice in names
            for e in runtime:
                assert hashlib.sha256(z.read(prefix + e['path'])).hexdigest() == e['sha256'].lower()
            build = json.loads(z.read(prefix + 'licenses/FFMPEG-VEYRA-BUILD.json').decode('utf-8-sig'))
            for e in build['files']:
                assert hashlib.sha256(z.read(prefix + e['name'])).hexdigest() == e['sha256'].lower()
            assert prefix + 'licenses/DAV1D-COPYRIGHT.txt' in names
        else:
            entries = json.loads(z.read('source-manifest.json'))
            for e in entries:
                assert hashlib.sha256(z.read(e['path'])).hexdigest() == e['sha256'].lower()
            for name in names:
                assert PurePosixPath(name).suffix.lower() not in {'.exe', '.dll', '.lib', '.obj', '.pdb', '.onnx', '.pth', '.addon64', '.so', '.a'}
                if PurePosixPath(name).suffix.lower() == '.mp4':
                    z.read(name).decode('utf-8')  # FFmpeg FATE textual checksum references.
            if 'FFmpeg' in archive.name:
                assert any(n.startswith('dav1d-source/') for n in names)
                assert '--enable-libdav1d' in z.read('binary-configuration.txt').decode()
        results.append(dict(name=archive.name, size=archive.stat().st_size, sha256=digest, files=len(names)))
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(json.dumps(dict(passed=True, archives=results), indent=2), encoding='utf-8')
print(json.dumps(results, indent=2))
