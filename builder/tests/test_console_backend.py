"""Run the console's real data loader with SDK stubs on a Linux host.

Tests regional acceptance and embedded-pack precedence without ROMs or a 3DS.
macOS: run this test in the project's build Docker image.
"""
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from emerald3ds_builder import pak

ROOT = Path(__file__).resolve().parents[2]
SPANISH = 'fe1558a3dcb0360ab558969e09b690888b846dd9'
ENGLISH = 'f3ae088181bf583e55daf962a92bb46f4f1d07b7'


@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('cc'), 'requires Linux libc and cc')
class ConsoleBackendTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.tmp.name)
        cls.bins = {}
        for language in (2, 7):
            root = cls.root / str(language)
            source = root / '3ds_port/src'
            source.mkdir(parents=True)
            shutil.copy2(ROOT / '3ds_port/src/3ds_data.c', source / '3ds_data.c')
            config = root / 'include/constants'
            config.mkdir(parents=True)
            (config / 'global.h').write_text('#define LANGUAGE_SPANISH 7\n#define GAME_LANGUAGE %d\n' % language)
            (root / '3ds.h').write_text('typedef int LightLock;\n'
                'static inline void LightLock_Init(LightLock *p) { *p=0; }\n'
                'static inline void LightLock_Lock(LightLock *p) { (void)p; }\n'
                'static inline void LightLock_Unlock(LightLock *p) { (void)p; }\n')
            (root / 'main.c').write_text(r'''
#include <stdlib.h>
#include <string.h>
#include "3ds_data.h"
#include "3ds_platform.h"
void CtrLog_Write(CtrLogCategory category, const char *format, ...) { (void)category; (void)format; }
FILE *CtrFs_OpenAsset(const char *path) {
    char full[256]; snprintf(full, sizeof(full), "romfs:/%s", path); return fopen(full, "rb");
}
int main(int argc, char **argv) {
    if (argc!=2) return 2;
    bool ok = CtrData_Init();
    if (!atoi(argv[1])) {
        if (ok) return 3;
        return strstr(CtrData_ErrorTitle(), "another ROM") == NULL ? 4 : 0;
    }
    if (!ok || CtrData_GetBackend()!=CTR_DATA_PAK) return 5;
    uint32_t size=0; void *data=CtrData_Load("game/test", &size);
    int result = data==NULL || size!=4 || memcmp(data, "TEST",4) ? 6 : 0;
    free(data); CtrData_Shutdown(); return result;
}
''')
            binary = root / 'loader'
            subprocess.run(['cc', '-std=gnu99', '-Wall', '-Wextra', '-Werror', '-I', str(root),
                            '-I', str(ROOT / '3ds_port/include'), str(source / '3ds_data.c'),
                            str(ROOT / '3ds_port/src/3ds_pak.c'), str(root / 'main.c'),
                            '-o', str(binary)], check=True)
            cls.bins[language] = binary

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_loader(self, language, sd, embedded=None, loose=False, accepted=True):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'romfs:/engine').mkdir(parents=True)
            (root / 'sdmc:/3ds/emerald3ds').mkdir(parents=True)
            (root / 'romfs:/engine/abi.bin').write_bytes(struct.pack('<I', 42))
            pak.write_pak(root / 'sdmc:/3ds/emerald3ds/emerald3ds.pak',
                          [('game/test', b'TEST')], 42, bytes.fromhex(sd))
            if embedded:
                pak.write_pak(root / 'romfs:/emerald3ds.pak', [('game/test', b'TEST')],
                              42, bytes.fromhex(embedded))
            if loose:
                marker = root / 'sdmc:/3ds/emerald3ds/devdata/.emerald3ds-dev'
                marker.parent.mkdir()
                marker.touch()
            subprocess.run([str(self.bins[language]), '1' if accepted else '0'], cwd=root, check=True)

    def test_spanish_sd_is_accepted(self):
        self.run_loader(7, SPANISH)

    def test_wrong_locale_is_refused(self):
        self.run_loader(7, ENGLISH, accepted=False)
        self.run_loader(2, SPANISH, accepted=False)

    def test_english_sd_still_works(self):
        self.run_loader(2, ENGLISH)

    def test_cia_pack_wins_over_sd_pack_and_loose_files(self):
        self.run_loader(7, ENGLISH, embedded=SPANISH, loose=True)

    def test_wrong_embedded_pack_never_falls_back_to_sd(self):
        self.run_loader(7, SPANISH, embedded=ENGLISH, accepted=False)
