"""Force both real fallback entrances and require the strict pre-execution exit."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--program', type=Path, required=True)
p.add_argument('--path', choices=('interp', 'microstep'), required=True)
args = p.parse_args()
with tempfile.TemporaryDirectory(prefix='wkc2-fallback-guard-') as directory:
    fixture = str(Path(directory) / 'census.tsv')
    env = dict(os.environ, SPU_FALLBACK_STRICT='1')
    env.pop('SPU_INTERP_PROFILE', None)
    command = [str(args.program.resolve())]
    if args.path == 'interp':
        command += [fixture, '0']
    else:
        command += ['--test-spu-microstep-profile', fixture]
    result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=10)
    marker = '[spu-fallback-forbidden] path=' + args.path
    assert result.returncode == 86, (result.returncode, result.stderr[-2000:])
    assert marker in result.stderr, result.stderr[-2000:]
    assert not Path(fixture).exists(), 'Profiling must not be needed by strict mode'
print('Strict mode rejected ' + args.path + ' entrance before executing guest instructions')
