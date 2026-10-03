#ifndef CTR_LOCALE_H
#define CTR_LOCALE_H

#include "constants/global.h"

#if GAME_LANGUAGE == LANGUAGE_SPANISH
#define CTR_TEXT(english, spanish) (spanish)
#else
#define CTR_TEXT(english, spanish) (english)
#endif

#endif
