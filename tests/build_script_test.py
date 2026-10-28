"""Exercise the macOS launcher without installing packages or opening a real app."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'build.sh'

class BuildScriptTests(unittest.TestCase):
    def test_build_install_and_launch_with_spaces(self):
        self.run_mock(False)

    def test_no_run(self):
        self.run_mock(True)

    def run_mock(self, no_run):
        with tempfile.TemporaryDirectory(prefix='cold flame build ') as directory:
            root = Path(directory)
            binaries = root / 'bin'
            binaries.mkdir()
            log = root / 'commands.jsonl'
            import sys
            mock = '#!' + sys.executable + '\n' + '''import json,os,sys
from pathlib import Path
name=Path(sys.argv[0]).name
args=sys.argv[1:]
with open(os.environ['MOCK_LOG'],'a') as f: f.write(json.dumps([name,*args])+'\\n')
if name=='uname': print('Darwin')
elif name=='sysctl': print('4')
elif name=='brew':
    if args[:1]==['--prefix']: print(os.environ['MOCK_PREFIX'])
    elif args[:2]==['list','--versions']: sys.exit(1)
elif name=='cmake' and '-B' in args:
    (Path(args[args.index('-B')+1])/'cold-flame.app').mkdir(parents=True)
'''
            for name in ['brew', 'uname', 'sysctl', 'cmake', 'open']:
                path = binaries / name
                path.write_text(mock)
                path.chmod(0o755)
            env = dict(os.environ, PATH=str(binaries)+':'+os.environ['PATH'], MOCK_PREFIX=str(root), MOCK_LOG=str(log))
            args = ['bash', str(SCRIPT), '--build-dir', str(root/'output with spaces')]
            if no_run:
                args += ['--no-run']
            args += ['--', '/tmp/show with spaces.cfl']
            result = subprocess.run(args, env=env, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            calls = [json.loads(line) for line in log.read_text().splitlines()]
            self.assertIn(['brew','install','cmake','ninja','qt@6','zlib'], calls)
            self.assertTrue(any(c[0]=='cmake' and '--build' in c for c in calls))
            launches = [c for c in calls if c[0]=='open']
            self.assertEqual(len(launches), 0 if no_run else 1)
            if not no_run:
                self.assertEqual(launches[0][-2:], ['--args','/tmp/show with spaces.cfl'])

if __name__ == '__main__':
    unittest.main()
