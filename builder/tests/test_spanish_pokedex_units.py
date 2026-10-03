"""Exercise the metric branches added to the native Pokédex patch."""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which('cc'), 'requires a host C compiler')
class MetricPokedexTests(unittest.TestCase):
    def test_native_height_and_weight_use_metric_units(self):
        patch = (ROOT / 'patches/pokeemerald/0011-spanish-native-pokedex-units.patch').read_text()
        branches = re.findall(r'\+#if defined\(PORT_BRIDGE\) && GAME_LANGUAGE == LANGUAGE_SPANISH\n(.*?)\+#else', patch, re.S)
        self.assertEqual(len(branches), 2)
        code = '''
#include <stdio.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8; typedef uint16_t u16;
#define STR_CONV_MODE_LEFT_ALIGN 0
#define CHAR_PERIOD '.'
#define CHAR_SPACE ' '
#define CHAR_0 '0'
#define CHAR_m 'm'
#define CHAR_k 'k'
#define CHAR_g 'g'
#define EOS 0
static char result[16];
static u8 *ConvertIntToDecimalStringN(u8 *dest, int value, int mode, int digits) {
    (void)mode; (void)digits;
    return dest + sprintf((char *)dest, "%d", value);
}
static void PrintInfoScreenText(const u8 *text, u8 left, u8 top) {
    (void)left; (void)top; strcpy(result, (const char *)text);
}
'''
        for name, parameter, body in zip(('Height', 'Weight'), ('height', 'weight'), branches):
            body = '\n'.join(line[1:] for line in body.splitlines())
            code += 'static void PrintMon%s(u16 %s, u8 left, u8 top) {\n%s\n}\n' % (name, parameter, body)
        code += '''
int main(void) {
    PrintMonHeight(20, 0, 0); if (strcmp(result,"2.0 m")) return 1;
    PrintMonHeight(1, 0, 0); if (strcmp(result,"0.1 m")) return 2;
    PrintMonHeight(145, 0, 0); if (strcmp(result,"14.5 m")) return 3;
    PrintMonWeight(60, 0, 0); if (strcmp(result,"6.0 kg")) return 4;
    PrintMonWeight(9500, 0, 0); if (strcmp(result,"950.0 kg")) return 5;
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'units.c').write_text(code)
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(root / 'units.c'),
                            '-o', str(root / 'units')], check=True)
            subprocess.run([str(root / 'units')], check=True)
