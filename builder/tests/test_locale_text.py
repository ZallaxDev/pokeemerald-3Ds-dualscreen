"""Test the port's UTF-8 adapter without the SDK or any cartridge data."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CC = shutil.which("cc") or shutil.which("gcc")


@unittest.skipUnless(CC, "requires a host C compiler")
class LocaleTextTests(unittest.TestCase):
    def test_encoding_bounds_and_language_selection(self):
        code = r'''
#include <assert.h>
#include <string.h>
#include "3ds_locale.h"
#include "3ds_locale_text.h"

static void expect(const char *text, const uint8_t *want, size_t size)
{
    uint8_t out[64];
    memset(out, 0x77, sizeof(out));
    assert(CtrLocale_ToGame(out, sizeof(out), text) == size);
    assert(memcmp(out, want, size) == 0);
    assert(out[size] == 0xff);
    assert(out[size + 1] == 0x77);
}

int main(void)
{
    /* These are adapter codes, not font pixels or cartridge strings. */
    const uint8_t ascii[] = {0xBB, 0xBC, 0xD5, 0xD6, 0xA1, 0xAA, 0x00,
                            0xBA, 0xAE, 0xAD, 0xF0, 0xAB, 0xAC, 0xB4, 0x1B};
    const uint8_t french[] = {0x01, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                             0x0B, 0x0C, 0x0F, 0x11, 0x13, 0x16, 0x68,
                             0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x20, 0x21,
                             0x24, 0x26, 0x28};
    const uint8_t special[] = {0x29, 0x14, 0xF6, 0xF3, 0xB8, 0x5B, 0x2D,
                              0xB4, 0xB0, 0xB5, 0xB6};
    const uint8_t fallback[] = {0xAC};
    uint8_t tiny[4] = {0x77, 0x77, 0x77, 0x77};

    expect("ABab09 /-.:!?'*", ascii, sizeof(ascii));
    expect("ÀÂÇÈÉÊËÎÏÔÙÛàâçèéêëîïôùû", french, sizeof(french));
    expect("ñÑüÜ,%&’…♂♀", special, sizeof(special));
    expect("\xC3", fallback, 1);             /* incomplete code point */
    expect("\xE0\x80\xAF", fallback, 1);     /* overlong slash */
    expect("\xED\xA0\x80", fallback, 1);     /* surrogate */
    expect("\xF4\x90\x80\x80", fallback, 1); /* outside Unicode */
    expect("\xF0\x9F\x98\x80", fallback, 1); /* unsupported full character */
    expect("", fallback, 0);
    assert(CtrLocale_ToGame(NULL, 0, "é") == 0);
    assert(CtrLocale_ToGame(tiny, 1, "é") == 0);
    assert(tiny[0] == 0xff && tiny[1] == 0x77);
    assert(CtrLocale_ToGame(tiny, 3, "Été") == 2);
    assert(tiny[0] == 0x06 && tiny[1] == 0xE8 && tiny[2] == 0xff && tiny[3] == 0x77);
    assert(CtrLocale_ToGame(tiny, 2, "éé") == 1);
    assert(tiny[0] == 0x1B && tiny[1] == 0xff);

#if GAME_LANGUAGE == LANGUAGE_FRENCH
    assert(strcmp(CTR_TEXT("EN", "ES", "FR"), "FR") == 0);
#elif GAME_LANGUAGE == LANGUAGE_SPANISH
    assert(strcmp(CTR_TEXT("EN", "ES", "FR"), "ES") == 0);
#else
    assert(strcmp(CTR_TEXT("EN", "ES", "FR"), "EN") == 0);
#endif
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "constants").mkdir()
            (root / "constants/global.h").write_text(
                "#define LANGUAGE_ENGLISH 2\n#define LANGUAGE_FRENCH 3\n"
                "#define LANGUAGE_SPANISH 7\n", encoding="utf-8")
            source = root / "locale.c"
            source.write_text(code, encoding="utf-8")
            for language in (2, 3, 7):
                with self.subTest(language=language):
                    executable = root / "locale.exe"
                    subprocess.run([CC, "-std=c99", "-Wall", "-Wextra", "-Werror",
                                    "-I" + str(root), "-I" + str(ROOT / "3ds_port/include"),
                                    "-DGAME_LANGUAGE=" + str(language), str(source),
                                    "-o", str(executable)], check=True)
                    subprocess.run([str(executable)], check=True)
