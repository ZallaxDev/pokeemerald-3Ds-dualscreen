#ifndef CTR_LOCALE_H
#define CTR_LOCALE_H

#include "constants/global.h"

/* Native UI literals are converted from UTF-8 into the game font encoding. */
#if GAME_LANGUAGE == LANGUAGE_SPANISH
#define CTR_TEXT(english, spanish, french, italian, japanese) (spanish)
#elif GAME_LANGUAGE == LANGUAGE_FRENCH
#define CTR_TEXT(english, spanish, french, italian, japanese) (french)
#elif GAME_LANGUAGE == LANGUAGE_ITALIAN
#define CTR_TEXT(english, spanish, french, italian, japanese) (italian)
#elif GAME_LANGUAGE == LANGUAGE_JAPANESE
#define CTR_TEXT(english, spanish, french, italian, japanese) (japanese)
#else
#define CTR_TEXT(english, spanish, french, italian, japanese) (english)
#endif

#endif
