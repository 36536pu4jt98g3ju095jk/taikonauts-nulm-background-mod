#ifndef GAME_CLEAR_H
#define GAME_CLEAR_H

/* Observes the game's own soul-gauge clear decision.

   TaikoNauts calls Gauge.IsGaugeClear once per player gauge. The function is
   located by its machine code (not by a fixed address) and wrapped so that
   every result the game itself computes is recorded. Nothing is called from
   the mod, and nothing is patched when the code is not recognised. */

/* Returns 1 when the function was found and hooked. The optional message
   describes the result and may be logged. */
int game_clear_install(char *message, unsigned int message_size);
void game_clear_remove(void);

/* The most recent result the game computed (1 = cleared), or -1 before the first
   call. Gauge objects may move in memory, so results are not tracked per
   object; with one player the latest result is that player's. */
int game_clear_latest(void);

/* Forgets the latest result, e.g. when a new song starts. */
void game_clear_forget(void);

/* Number of calls observed so far, for diagnostics. */
unsigned int game_clear_calls(void);

#endif
