import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import setup


class SetupTest(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='repack test ')
        self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name).resolve()
        self.disc=self.root/'disc';self.disc.mkdir()
        (self.disc/'default.xbe').write_bytes(b'owned fixture')
        (self.disc/'vc_53450030').mkdir()
        self.installer=self.root/'setup.exe';self.installer.write_bytes(b'fixture')
        self.steam=self.root/'Steam';(self.steam/'steamapps').mkdir(parents=True)
        self.proton=self.root/'proton';self.proton.write_text('#!/bin/sh\nexit 0\n');self.proton.chmod(0o755)
        self.output=self.root/'clean install'
        self.compiler=self.root/'d3dcompiler_47.dll';self.compiler.write_bytes(b'MZfixture')

    def install(self):
        return setup.setup(self.installer,self.disc,self.proton,self.steam,self.output,self.compiler)

    def fake_install(self,*args,**kwargs):
        game=self.output/'game';game.mkdir(exist_ok=True);(game/'NFL2K5.exe').write_bytes(b'MZfixture')
        return subprocess.CompletedProcess(args,0)

    def test_isolated_install_and_safe_launch(self):
        with patch('setup.subprocess.run',side_effect=self.fake_install) as call:
            result=self.install()
        self.assertEqual(result['status'],'installed-not-gameplay-verified')
        self.assertEqual((self.output/'game/original/disc').resolve(),self.disc)
        cmd=call.call_args.args[0]
        self.assertIn('/DIR='+setup.win(self.output/'game'),cmd)
        self.assertEqual((self.output/'game/d3dcompiler_47.dll').read_bytes(),b'MZfixture')
        user=self.output/'prefix/pfx/drive_c/users/steamuser/AppData/Local/NFL2K5-PC'
        self.assertEqual((user/'mods/presentation.ini').read_text(),'package = ESPN (Original)\n')
        self.assertIn('scale = 2', (user/'nfl2k5_video.ini').read_text())
        self.assertEqual((self.disc/'default.xbe').read_bytes(),b'owned fixture')
        # Replace only the fixture Proton script with an argv/env observer.
        self.proton.write_text('#!/usr/bin/env python3\nimport os,sys,json\nprint(json.dumps({"argv":sys.argv[1:],"cwd":os.getcwd(),"prefix":os.environ["STEAM_COMPAT_DATA_PATH"],"id":os.environ.get("SteamGameId"),"custom":[k for k in os.environ if k.startswith(("NFL2K5_","RECOMP_"))],"overrides":os.environ.get("WINEDLLOVERRIDES")}))\n')
        env=dict(os.environ,SteamGameId='123',NFL2K5_ROOT='bad',RECOMP_SHADOW_LIVE='bad')
        r=subprocess.run([str(self.output/'play.sh'),'--validate'],env=env,capture_output=True,text=True,check=True)
        observed=json.loads(r.stdout)
        self.assertEqual(observed['custom'],[])
        self.assertEqual(observed['id'],'123')
        self.assertEqual(observed['overrides'],'d3d11,dxgi=b;d3dcompiler_47=n,b')
        self.assertEqual(observed['cwd'],str(self.output/'game'))
        self.assertEqual(observed['argv'],['runinprefix',str(self.output/'game/NFL2K5.exe'),'--validate'])

    def test_existing_output_refused_before_launch(self):
        self.output.mkdir()
        with patch('setup.subprocess.run') as call:
            with self.assertRaises(FileExistsError):self.install()
            call.assert_not_called()

    def test_failure_preserves_logs_without_play_script(self):
        with patch('setup.subprocess.run',return_value=subprocess.CompletedProcess([],4)):
            with self.assertRaises(RuntimeError):self.install()
        self.assertTrue((self.output/'setup-proton.log').exists())
        self.assertFalse((self.output/'play.sh').exists())

    def test_incomplete_disc_refused(self):
        (self.disc/'vc_53450030').rmdir()
        with self.assertRaises(ValueError):self.install()
        self.assertFalse(self.output.exists())

    def test_environment_keeps_steam_identity(self):
        e=setup.clean_env(self.proton,self.steam,self.output,{'SteamGameId':'123','NFL2K5_ROOT':'x','RECOMP_SHADOW_BOX':'x','WINEPREFIX':'old'})
        self.assertEqual(e['SteamGameId'],'123')
        self.assertNotIn('NFL2K5_ROOT',e);self.assertNotIn('RECOMP_SHADOW_BOX',e);self.assertNotIn('WINEPREFIX',e)

if __name__=='__main__':unittest.main()
