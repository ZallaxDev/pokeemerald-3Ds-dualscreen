"""Exercise the metric branches added to the native Pokédex patch.

The Spanish and the French ROMs print heights and weights in metres and
kilograms; the English one uses feet/inches and pounds. The patch carries the
metric branch for both languages, so this test also checks the guard covers
them.
"""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


CC = shutil.which('cc') or shutil.which('gcc')


@unittest.skipUnless(CC, 'requires a host C compiler')
class MetricPokedexTests(unittest.TestCase):
    def test_native_height_and_weight_use_metric_units(self):
        name = 'patches/pokeemerald/0035-spanish-native-pokedex-units.patch'
        # The public repository's layout, or the private workspace's.
        path = next(p for p in (ROOT / name, ROOT / 'public' / name) if p.exists())
        patch = path.read_text(encoding='utf-8')
        guards = re.findall(r'\+#if defined\(PORT_BRIDGE\) && \(([^)]*)\)\n', patch)
        self.assertEqual(len(guards), 2)
        for guard in guards:
            self.assertIn('GAME_LANGUAGE == LANGUAGE_SPANISH', guard)
            self.assertIn('GAME_LANGUAGE == LANGUAGE_FRENCH', guard)
        branches = re.findall(r'\+#if defined\(PORT_BRIDGE\)[^\n]*\n(.*?)\+#else', patch, re.S)
        self.assertEqual(len(branches), 2)
        code = '''
#include <stdio.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8; typedef uint16_t u16;
#define STR_CONV_MODE_LEFT_ALIGN 0
#define CHAR_PERIOD '.'
#define CHAR_COMMA ','
#define LANGUAGE_FRENCH 3
#define LANGUAGE_SPANISH 7
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
        followup = (ROOT / 'patches/pokeemerald/0045-french-pokedex-number-format.patch').read_text(
            encoding='utf-8')
        hunks = re.findall(r'^@@[^\n]*\n((?:[ +\-][^\n]*\n|\n)+)', followup, re.M)
        for name, parameter, body in zip(('Height', 'Weight'), ('height', 'weight'), branches):
            body = '\n'.join(line[1:] for line in body.splitlines())
            # Apply the actual separator hunks to the original metric bodies.
            # Test fixtures carry neither original cartridge text nor graphics.
            applied = 0
            for hunk in hunks:
                if '+    *end++ = CHAR_COMMA;' not in hunk:
                    continue
                lines = hunk.splitlines()
                before = '\n'.join(line[1:] for line in lines if not line or line[0] in ' -')
                after = '\n'.join(line[1:] for line in lines if not line or line[0] in ' +')
                if before in body:
                    body = body.replace(before, after, 1)
                    applied += 1
            self.assertEqual(applied, 1)
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
            for language in (3, 7):
                with self.subTest(language=language):
                    variant = code
                    if language == 3:
                        variant = re.sub(r'"(\d+)\.(\d+ (?:m|kg))"', r'"\1,\2"', variant)
                    (root / 'units.c').write_text(variant, encoding='utf-8')
                    executable = root / 'units.exe'
                    subprocess.run([CC, '-std=c99', '-Wall', '-Wextra', '-Werror',
                                    '-DGAME_LANGUAGE=' + str(language), str(root / 'units.c'),
                                    '-o', str(executable)], check=True)
                    subprocess.run([str(executable)], check=True)
