"""Bundle a clean Git commit and audited, unchanged dependency source archives."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--temp', type=Path, required=True)
    parser.add_argument('--dependency-directory', type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r'\d+\.\d+\.\d+', args.version):
        raise ValueError('Invalid version')
    def git(*argv):
        return subprocess.check_output(['git', '-C', str(args.root), *argv]).decode().strip()
    if git('status', '--porcelain'):
        raise RuntimeError('Source release requires a clean worktree')
    commit = git('rev-parse', 'HEAD')
    dependencies = {
        'Veyra-1.4.1-FFmpeg-source.zip': '15a77217bedceb4280fc680d5c0761544170356ca85e871f1558b61c86ee5f02',
        'Veyra-1.4.1-RemotePlay-source.zip': '272e41bc128c9bd86d7a9a93e4f0150dd92db6b9d00909643336b9e01561f166',
    }
    for name, expected in dependencies.items():
        if sha256(args.dependency_directory / name) != expected:
            raise RuntimeError(f'Dependency source identity mismatch: {name}')
    args.output.mkdir(parents=True, exist_ok=True)
    args.temp.mkdir(parents=True, exist_ok=True)
    prefix = f'Veyra-{args.version}-source'
    output = args.output / (prefix + '.zip')
    intermediate = args.temp / f'{prefix}-{commit}.zip'
    if output.exists() or intermediate.exists():
        raise FileExistsError('Refusing to overwrite source archives')
    subprocess.run(['git', '-C', str(args.root), 'archive', '--format=zip',
                    f'--output={intermediate}', commit], check=True)
    records = []
    with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED) as dest:
        with zipfile.ZipFile(intermediate) as source:
            for entry in source.infolist():
                if entry.is_dir():
                    continue
                name = entry.filename
                if re.search(r'\.(dll|exe|lib|pdb|obj|onnx|pth|zip|7z)$|(^|/)(runtime_local|third_party_local)/', name, re.I):
                    raise RuntimeError(f'Forbidden source payload: {name}')
                data = source.read(entry)
                target = f'{prefix}/veyra/{name}'
                dest.writestr(target, data)
                records.append(dict(path=target, size=len(data), sha256=hashlib.sha256(data).hexdigest()))
        for name, digest in dependencies.items():
            path = args.dependency_directory / name
            target = f'{prefix}/dependencies/{name}'
            dest.write(path, target, compress_type=zipfile.ZIP_STORED)
            records.append(dict(path=target, size=path.stat().st_size, sha256=digest))
        manifest = dict(version=args.version, commit=commit, snapshot='git archive HEAD', files=records)
        dest.writestr(f'{prefix}/source-manifest.json', json.dumps(manifest, indent=2))
    with zipfile.ZipFile(output) as archive:
        if len(archive.infolist()) != len(records) + 1:
            raise RuntimeError('Source entry count mismatch')
        for record in records:
            entry = archive.getinfo(record['path'])
            with archive.open(entry) as stream:
                actual = hashlib.file_digest(stream, 'sha256').hexdigest()
            if entry.file_size != record['size'] or actual != record['sha256']:
                raise RuntimeError(f'Source payload mismatch: {record["path"]}')
    digest = sha256(output)
    output.with_suffix('.zip.sha256').write_text(f'{digest}  {output.name}\n', encoding='ascii')
    intermediate.unlink()
    print(json.dumps(dict(archive=str(output), commit=commit, bytes=output.stat().st_size,
                          sha256=digest, verifiedFiles=len(records)), indent=2))


if __name__ == '__main__':
    main()
