import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('voice_import',Path(__file__).with_name('import_hotkeys_voice_transcripts.py'))
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

class VoiceImportTests(unittest.TestCase):
    def test_paths_are_canonical_and_full(self):
        self.assertEqual(module.audio_path('Vo\\D\\M\\Atk_DM001.mp3,'),'sound/vo/d/m/atk_dm001.mp3')
        self.assertEqual(module.audio_path('Sound/Vo/Misc/some file.mp3'),'sound/vo/misc/some file.mp3')
        for path in ['https://host/file.mp3','../vo/file.mp3','/vo/file.mp3','vo//file.mp3','vo/file.mp3.exe','vo/\0file.mp3']:
            self.assertIsNone(module.audio_path(path),path)

    def test_all_columns_and_sheets_keep_distinct_audio_paths(self):
        result,report=module.import_rows([
            ('Elf',[(2,{1:'File',2:'Text',4:'File',5:'Text'}),
                    (3,{1:'vo/a/f/hello.mp3',2:'Female words',4:'vo/a/m/hello.mp3',5:'Male words'}),
                    (4,{1:'vo/a/f/hello.mp3',2:'Alternative words'})]),
            ('Mod',[(1,{2:'File',3:'Text'}),(2,{2:'mod/vo/hello.mp3,',3:'Mod words'})])])
        self.assertEqual(len(result),3)
        self.assertEqual(set(result['sound/vo/a/f/hello.mp3']),{'Female words','Alternative words'})
        self.assertEqual(result['sound/vo/a/m/hello.mp3'],['Male words'])
        self.assertEqual(report['sheets'],['Elf','Mod'])
        self.assertIn('sound/vo/a/f/hello.mp3',report['conflicts'])
        self.assertEqual(len(report['repaired']),1)

    def test_missing_text_and_bad_filenames_are_reported(self):
        result,report=module.import_rows([('One',[(1,{1:'File',2:'Text'}),
            (2,{1:'vo/missing.mp3',2:''}),(3,{1:'../escape.mp3',2:'words'})])])
        self.assertFalse(result)
        self.assertEqual(len(report['rejected']),2)

    def test_generated_data_escapes_code_and_retains_unicode(self):
        rendered=module.render({'sound/vo/a.mp3':['"quote" \\ slash café\0']},{},{},'digest')
        self.assertIn('café',rendered)
        self.assertIn('\\"quote\\"',rendered)
        self.assertIn('\\\\ slash',rendered)
        self.assertNotIn('\\u0000',rendered)
        self.assertNotIn('\0',rendered)
        self.assertIn(module.SOURCE,rendered)

if __name__=='__main__':unittest.main()
