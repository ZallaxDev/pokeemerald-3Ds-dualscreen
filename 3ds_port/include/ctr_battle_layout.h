#ifndef CTR_BATTLE_LAYOUT_H
#define CTR_BATTLE_LAYOUT_H

/*
 * Where the 2D battle puts its two sides on the top screen (3DS only).
 *
 * The battle scene is shown magnified (CTR_BATTLE_ZOOM) above a text box
 * drawn 1:1 along the bottom of the screen. That box floats over the scene
 * (3ds_video.c, DrawBattleText), so the scene carries on behind its edges and
 * the screen has room the GBA's 240x160 did not: the opponent's side - its
 * Pokemon, trainers, terrain platform and info boxes - is moved up, and the
 * player's side moved down a little, into the band the GBA's dark border
 * took.
 *
 * Sprites move in the game's own coordinates (battle_anim_mons.c,
 * CtrBattleSideShift), so every animation aimed at a battler follows. The
 * terrain moves in the compositor: the battle terrain has the opponent's
 * platform above line CTR_BATTLE_SIDE_SPLIT and the player's below it - the
 * line the battle intro already slides the two halves apart at - and each
 * half is drawn moved by its side's shift, the lines left between them
 * filled from the open ground just below the split, four lines repeated
 * (the terrain's stripes repeat every four lines).
 *
 * In GBA pixels. Contests use the same animation code and keep the GBA's
 * places.
 */
#define CTR_BATTLE_ENEMY_RISE 8
#define CTR_BATTLE_PLAYER_DROP 2
#define CTR_BATTLE_SIDE_SPLIT 80

/* The screen line the battle text box's frame starts at. */
#define CTR_BATTLE_BOX_TOP 195

#ifdef PLATFORM_3DS
/*
 * The vertical shift of a battler's side: up for the opponent, down for the
 * player, nothing in a contest. Defined in battle_anim_mons.c.
 */
int CtrBattleSideShift(unsigned char battler);
/* The same for a side (B_SIDE_PLAYER, B_SIDE_OPPONENT). */
int CtrBattleSideShiftOf(unsigned char side);
#endif

#endif
