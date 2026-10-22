#!/usr/bin/env python3
"""Build the approved relay in Ubuntu 26.04 without installing Ubuntu host packages."""
import argparse
import ipaddress
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from urllib.parse import urlsplit


REPOSITORY = Path(__file__).resolve().parents[1]
IMAGE = 'localhost/uu-plus-reference-relay:26.04'
CONTEXT_FILES = (
    'scripts/Containerfile.reference-relay',
    'scripts/prepare-build-toolchain.py',
    'patches/reference-build-packages.json',
    'vendor/freerdp-sdl-build/source-lock.json',
)
PROXY_VARIABLES = ('HTTP_PROXY', 'HTTPS_PROXY', 'ALL_PROXY', 'NO_PROXY',
                   'http_proxy', 'https_proxy', 'all_proxy', 'no_proxy')


def loopback_proxy():
    for name in PROXY_VARIABLES:
        value = os.environ.get(name)
        if not value or name.lower() == 'no_proxy':
            continue
        host = urlsplit(value if '://' in value else 'http://' + value).hostname
        if host == 'localhost':
            return True
        try:
            if host and ipaddress.ip_address(host).is_loopback:
                return True
        except ValueError:
            pass
    return False


def paths_for_build(output, work):
    output, work = output.expanduser().resolve(), work.expanduser().resolve()
    for name, path in [('output', output), ('work directory', work)]:
        source_directory = (path.is_relative_to(REPOSITORY) and
                            not path.is_relative_to(REPOSITORY / 'build'))
        if (REPOSITORY.is_relative_to(path) or source_directory or
                any(c in str(path) for c in ',\n\r')):
            raise ValueError(f'Choose a separate {name} directory; cannot mount {path}')
        if path.exists() and not path.is_dir():
            raise ValueError(f'The {name} must be a directory: {path}')
    if output.is_relative_to(work) or work.is_relative_to(output):
        raise ValueError('The output and work directories must not overlap')
    if any(c in str(REPOSITORY) for c in ',\n\r'):
        raise ValueError('The repository path contains unsupported mount separators')
    return output, work


def commands(engine, output, work, context):
    network = ['--network=host'] if loopback_proxy() else []
    build = [engine, 'build', *network]
    run = [engine, 'run', '--rm', *network]
    for name in PROXY_VARIABLES:
        if os.environ.get(name):
            # Pass names only: the engine reads values from its environment, so
            # proxy credentials never enter command-line arguments or dry runs.
            if engine == 'docker':
                build += ['--build-arg', name]
            run += ['--env', name]
    build += ['--tag', IMAGE, '--file',
              str(Path(context) / 'scripts/Containerfile.reference-relay'), str(context)]
    if Path(engine).name == 'podman':
        run.append('--userns=keep-id')
    run += ['--user', f'{os.getuid()}:{os.getgid()}',
            '--mount', f'type=bind,src={REPOSITORY},dst=/product,ro',
            '--mount', f'type=bind,src={output},dst=/output',
            '--mount', f'type=bind,src={work},dst=/build',
            '--env', 'UURB_BUILD_DIR=/build', '--env', 'HOME=/tmp',
            '--workdir', '/product', IMAGE,
            '/product/scripts/build-winpr.sh', '/output']
    return {'build': build, 'run': run}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', choices=('podman', 'docker'))
    parser.add_argument('--output', type=Path, default=REPOSITORY / 'build/freerdp')
    parser.add_argument('--work-dir', type=Path, default=REPOSITORY / 'build/reference-relay-work')
    parser.add_argument('--dry-run', action='store_true', help='print commands as JSON; change nothing')
    args = parser.parse_args()
    try:
        output, work = paths_for_build(args.output, args.work_dir)
        engine = args.engine or ('podman' if shutil.which('podman') else 'docker')
        if args.dry_run:
            print(json.dumps(commands(engine, output, work, '<temporary build context>'),
                             ensure_ascii=False, indent=2))
            return 0
        if os.getuid() == 0:
            raise ValueError('Run as the desktop user; do not use sudo for this builder')
        if not shutil.which(engine):
            raise ValueError('Install podman and uidmap, or provide a working Docker engine; '
                             'see docs/debian-13-host.md')
        validator = [sys.executable, str(REPOSITORY / 'scripts/verify-freerdp-runtime.py')]
        subprocess.run([*validator, '--mode', 'profile'], check=True)
        if subprocess.run([*validator, str(output)], stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode == 0:
            print(f'Approved relay is already current in {output}')
            return 0
        if engine == 'docker':
            information = subprocess.run(
                [engine, 'info', '--format', '{{json .SecurityOptions}}'],
                check=True, capture_output=True, text=True)
            options = json.loads(information.stdout)
            if not isinstance(options, list) or not all(isinstance(v, str) for v in options):
                raise ValueError('Could not inspect Docker user namespace configuration')
            if any('rootless' in v or 'userns' in v for v in options):
                raise ValueError('Docker user namespace configurations are not supported by '
                                 'this builder. Install podman and uidmap, then rerun with '
                                 '--engine podman to preserve bind-mount ownership')
        # Send only build recipes and public manifests to the engine, not the checkout.
        with tempfile.TemporaryDirectory(prefix='uu-reference-context-') as directory:
            context = Path(directory)
            for name in CONTEXT_FILES:
                destination = context / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(REPOSITORY / name, destination)
            plan = commands(engine, output, work, context)
            subprocess.run(plan['build'], check=True)
            output.mkdir(parents=True, exist_ok=True)
            work.mkdir(parents=True, exist_ok=True)
            subprocess.run(plan['run'], check=True)
        subprocess.run([*validator, str(output)], check=True)
        print(f'Approved relay built and verified in {output}')
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f'Reference relay build failed: {error}', file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            print(error.stderr.strip(), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
