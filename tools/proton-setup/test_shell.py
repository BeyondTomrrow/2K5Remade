"""Exercise the Bash user flow with fake downloads and installer execution."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT=Path(__file__).with_name('setup.sh').resolve()
EXPECTED='37600d962775721db113328844da5aa8148a0633b94b976d6a2eee6d1514a588'


class ShellTest(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='deck setup ')
        self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name).resolve()
        self.bin=self.root/'bin';self.bin.mkdir()
        self.home=self.root/'home';self.home.mkdir()
        self.proton=self.home/'.local/share/Steam/steamapps/common/Proton - Experimental/proton'
        self.proton.parent.mkdir(parents=True);self.proton.touch()
        self.disc=self.home/'my disc';self.disc.mkdir()
        (self.disc/'default.xbe').touch();(self.disc/'vc_53450030').mkdir()
        self.record=self.root/'args.json'
        self.env=dict(os.environ,HOME=str(self.home),XDG_CACHE_HOME=str(self.home/'.cache'),
                      PATH=str(self.bin)+os.pathsep+os.environ['PATH'])
        self.tool('uname',"print('Linux')")
        self.tool('python3',f"import json,sys;open({str(self.record)!r},'w').write(json.dumps(sys.argv[1:]))")
        self.tool('curl',"import sys;from pathlib import Path;Path(sys.argv[sys.argv.index('--output')+1]).write_text('cab' if sys.argv[-1].endswith('.cab') else 'fixture')")
        self.tool('sha256sum',f"import sys;from pathlib import Path;s=Path(sys.argv[-1]).read_text();print(({{'fixture':'{EXPECTED}','cab':'f736e161547095bb8d98c636b85fdfeb4070fefeee3c3745db3ce88f6eb1d9de','dll':'e994847e01a6f1e4cbdc5a864616ac262f67ee4f14db194984661a8d927ab7f4'}}.get(s,'wrong'))+'  fixture')")
        self.tool('bsdtar',"print('dll',end='')")
        self.tool('steamos-add-to-steam','pass')

    def tool(self,name,code):
        p=self.bin/name;p.write_text(f'#!{sys.executable}\n{code}\n');p.chmod(0o755)

    def run_script(self,*args,input=''):
        return subprocess.run(['bash',str(SCRIPT),*args],input=input,text=True,
                              env=self.env,capture_output=True)

    def test_full_prompt_flow_preserves_spaces(self):
        r=self.run_script(input=f'y\n\n{self.disc}\n\n')
        self.assertEqual(r.returncode,0,r.stderr)
        args=json.loads(self.record.read_text())
        self.assertEqual(args[args.index('--disc')+1],str(self.disc))
        self.assertIn('--add-to-steam',args)
        self.assertIn('--d3dcompiler',args)
        self.assertTrue((self.home/'.cache/2k5-repack-proton/v0.6.1-hotfix/NFL2K5-PC-Setup.exe').exists())

    def test_checksum_failure_never_runs_installer(self):
        self.tool('sha256sum',"print('wrong  fixture')")
        r=self.run_script(input=f'y\n\n{self.disc}\n\n')
        self.assertNotEqual(r.returncode,0)
        self.assertIn('checksum mismatch',r.stderr)
        self.assertFalse(self.record.exists())
        self.assertEqual(list((self.home/'.cache/2k5-repack-proton/v0.6.1-hotfix').iterdir()),[])

    def test_bad_cabinet_never_runs_installer(self):
        self.tool('sha256sum',f"import sys;from pathlib import Path;print(('{EXPECTED}' if Path(sys.argv[-1]).read_text()=='fixture' else 'wrong')+'  fixture')")
        r=self.run_script(input=f'y\n\n{self.disc}\n\n')
        self.assertNotEqual(r.returncode,0)
        self.assertIn('cabinet checksum mismatch',r.stderr)
        self.assertFalse(self.record.exists())

    def test_bad_compiler_never_runs_installer(self):
        self.tool('bsdtar',"print('dllwrong',end='')")
        r=self.run_script(input=f'y\n\n{self.disc}\n\n')
        self.assertNotEqual(r.returncode,0)
        self.assertIn('Shader compiler checksum mismatch',r.stderr)
        self.assertFalse(self.record.exists())

    def test_cancel_does_not_download(self):
        r=self.run_script(input='n\n')
        self.assertEqual(r.returncode,0)
        self.assertFalse((self.home/'.cache').exists())
        self.assertFalse(self.record.exists())

    def test_explicit_args_pass_through_without_prompt(self):
        r=self.run_script('--help')
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual(json.loads(self.record.read_text())[-1],'--help')

if __name__=='__main__':unittest.main()
