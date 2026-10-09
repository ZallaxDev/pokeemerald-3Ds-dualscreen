#ifndef CTR_LOCALE_H
#define CTR_LOCALE_H

#include "constants/global.h"

/* The touch screen labels are drawn with the port's own ASCII font, so they
 * must stay unaccented (see Ascii() in 3ds_bottom_ui.c). */
#if GAME_LANGUAGE == LANGUAGE_SPANISH
#define CTR_TEXT(english, spanish, french) (spanish)
#elif GAME_LANGUAGE == LANGUAGE_FRENCH
#define CTR_TEXT(english, spanish, french) (french)
#else
#define CTR_TEXT(english, spanish, french) (english)
#endif

#endif
