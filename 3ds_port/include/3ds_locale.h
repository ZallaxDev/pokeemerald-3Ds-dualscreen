#ifndef CTR_LOCALE_H
#define CTR_LOCALE_H

#include "constants/global.h"

/* Native UI literals are converted from UTF-8 into the game font encoding. */
#if GAME_LANGUAGE == LANGUAGE_SPANISH
#define CTR_TEXT(english, spanish, french, italian, german) (spanish)
#elif GAME_LANGUAGE == LANGUAGE_FRENCH
#define CTR_TEXT(english, spanish, french, italian, german) (french)
#elif GAME_LANGUAGE == LANGUAGE_ITALIAN
#define CTR_TEXT(english, spanish, french, italian, german) (italian)
#elif GAME_LANGUAGE == LANGUAGE_GERMAN
#define CTR_TEXT(english, spanish, french, italian, german) (german)
#else
#define CTR_TEXT(english, spanish, french, italian, german) (english)
#endif

#endif
