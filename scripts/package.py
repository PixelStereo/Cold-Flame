#!/usr/bin/env python3
"""Create native release downloads from a COLD_FLAME_DEPLOY installation."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(*args, **kwargs):
    subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def smoke_environment(platform="linux"):
    # Test the deployed payload, not Qt supplied by the build machine's environment.
    env = os.environ.copy()
    for key in ('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML_IMPORT_PATH',
                'QML2_IMPORT_PATH', 'LD_LIBRARY_PATH', 'DYLD_LIBRARY_PATH',
                'DYLD_FRAMEWORK_PATH'):
        env.pop(key, None)
    roots = [Path(env[key]).resolve() for key in ('QT_ROOT_DIR', 'QT_HOST_PATH') if env.get(key)]
    env['PATH'] = os.pathsep.join(p for p in env.get('PATH', '').split(os.pathsep)
                                  if p and not any(Path(p).resolve().is_relative_to(root) for root in roots))
    env['QT_QPA_PLATFORM'] = {'macos': 'cocoa', 'windows': 'windows'}.get(platform, 'offscreen')
    return env


def validate_payload(stage, platform):
    if platform == 'macos':
        executable = stage / 'cold-flame.app/Contents/MacOS/cold-flame'
        runtime = list((stage / 'cold-flame.app/Contents/Frameworks').glob('QtCore*'))
        plugins = list((stage / 'cold-flame.app/Contents/PlugIns/platforms').glob('*cocoa*'))
    elif platform == 'windows':
        executable = stage / 'bin/cold-flame.exe'
        runtime = list((stage / 'bin').glob('Qt6Core.dll'))
        plugins = list(stage.rglob('qwindows.dll'))
    else:
        executable = stage / 'bin/cold-flame'
        runtime = list((stage / 'lib').glob('libQt6Core.so*'))
        plugins = list(stage.rglob('libqxcb.so'))
    if not executable.is_file() or not runtime or not plugins:
        raise RuntimeError(f'Incomplete {platform} package: executable, Qt Core or platform plugin missing')
    return executable


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', required=True, choices=('macos', 'linux', 'windows'))
    parser.add_argument('--arch', required=True, choices=('arm64', 'x86_64', 'universal'))
    parser.add_argument('--version', required=True)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build')
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'dist')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,80}', args.version):
        parser.error('Version must contain only letters, numbers, dots, underscores and hyphens')
    expected = {'darwin': 'macos', 'linux': 'linux', 'win32': 'windows'}.get(sys.platform)
    if args.arch == 'universal' and args.platform != 'macos':
        parser.error('Universal packages are supported only on macOS')
    if expected != args.platform:
        parser.error('Packages must be built on their native operating system')
    build = args.build_dir.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    stage = build / 'release-stage'
    if stage.exists():
        shutil.rmtree(stage)
    run('cmake', '--install', build, '--config', 'Release', '--prefix', stage)
    executable = validate_payload(stage, args.platform)
    name = f'ColdFlame-{args.version}-{args.platform}-{args.arch}'
    assets = []
    if args.platform == 'macos':
        bundle = stage / 'cold-flame.app'
        if args.arch == 'universal':
            binaries = [executable]
            binaries.extend((bundle / 'Contents/Frameworks').glob('*.dylib'))
            binaries.extend((bundle / 'Contents/PlugIns').rglob('*.dylib'))
            for framework in (bundle / 'Contents/Frameworks').glob('*.framework'):
                binaries.append(framework / framework.stem)
            for binary in binaries:
                run('lipo', binary, '-verify_arch', 'arm64', 'x86_64')
        # Ad-hoc signatures support launching on Apple Silicon; these are not notarization.
        run('codesign', '--force', '--deep', '--sign', '-', bundle)
        run('codesign', '--verify', '--deep', '--strict', bundle)
        run(executable, '--smoke-test', env=smoke_environment(args.platform), timeout=30)
        destination = output / f'{name}.zip'
        run('ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', bundle, destination)
        assets.append(destination)
    elif args.platform == 'windows':
        run(executable, '--smoke-test', env=smoke_environment(args.platform), timeout=30)
        destination = output / f'{name}-setup.exe'
        makensis = shutil.which('makensis') or 'C:/Program Files (x86)/NSIS/makensis.exe'
        # The user picks at install time: all users (the default) or the current user only
        run(makensis, '/DPRODUCT=ColdFlame', f'/DAPP_VERSION={args.version.removeprefix("v")}', '/DAPP_EXE=bin\\cold-flame.exe',
            f'/DPAYLOAD={stage}', f'/DOUTPUT={destination}', ROOT / 'packaging/windows.nsi')
        # What users get is the installer: install it silently somewhere clean and start what it installed
        with tempfile.TemporaryDirectory() as clean:
            run(destination, '/S', '/CurrentUser', f'/D={clean}')
            run(Path(clean) / 'app/bin/cold-flame.exe', '--smoke-test', env=smoke_environment(args.platform), timeout=30)
        assets.append(destination)
    else:
        launcher = stage / 'AppRun'
        launcher.write_text('''#!/bin/sh
set -eu
app_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH="$app_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$app_dir/bin/cold-flame" "$@"
''')
        launcher.chmod(0o755)
        run(launcher, '--smoke-test', env=smoke_environment(), timeout=30)
        # Also test the real XCB platform plugin when Xvfb is available on CI.
        if shutil.which('xvfb-run'):
            env = smoke_environment()
            env['QT_QPA_PLATFORM'] = 'xcb'
            run('xvfb-run', '-a', launcher, '--smoke-test', env=env, timeout=30)
        destination = output / f'{name}.tar.gz'
        with tarfile.open(destination, 'w:gz') as archive:
            archive.add(stage, arcname=name)
        assets.append(destination)
    for asset in assets:
        digest = hashlib.sha256(asset.read_bytes()).hexdigest()
        asset.with_name(asset.name + '.sha256').write_text(f'{digest}  {asset.name}\n')
        print(asset)


if __name__ == '__main__':
    main()
