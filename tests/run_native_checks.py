#!/usr/bin/env python3
"""Run local, database-free native Hunts checks. All temporary output stays in the repository."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(command):
    subprocess.run(command, cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compile-commands', type=Path,
                        help='Optional reference AzerothCore compile_commands.json (read only)')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='.native-checks-', dir=ROOT) as output:
        for name, adapter, sources, defines in [
            ('transaction_wait_tests', 'transaction_wait_adapter', ['HuntCurrencyMigration'], []),
            ('native_currency_startup', 'currency_adapter', ['HuntCurrencyService', 'HuntCurrencyMigration'],
             ['-DNATIVE_HUNTS_MEMORY_DATABASE']),
        ]:
            executable = str(Path(output) / name)
            run(['g++', '-std=c++17', '-pthread', *defines, '-Itests/' + adapter, '-Isrc',
                 'tests/' + name + '.cpp', *['src/' + s + '.cpp' for s in sources], '-o', executable])
            run([executable])
        run(['python3', '-B', 'tests/test_extended_cost_epf.py'])
        import zipfile
        with zipfile.ZipFile(ROOT / 'content/mod-hunts.epf') as archive:
            assert json.loads(archive.read('manifest.json')) == json.loads(
                (ROOT / 'content/manifest.native.json').read_text())
        print('PASS authored/EPF manifests match', flush=True)
        if args.compile_commands:
            records = json.loads(args.compile_commands.read_text())
            record = next(r for r in records if 'mod-dungeon-quests' in r['file'])
            command = record.get('arguments') or shlex.split(record['command'])
            command = command[:command.index('-o')]
            command = [c for c in command if c not in ('-O3', '-DNDEBUG')]
            for source in sorted((ROOT / 'src').glob('*.cpp')):
                run([*command, '-I' + str(ROOT / 'src'), '-fsyntax-only', str(source)])
                print('PASS reference-core syntax:', source.name, flush=True)


if __name__ == '__main__':
    main()
