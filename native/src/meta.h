/* ===========================================================================
   POCKET ARCADE - cross-game meta layer (internal to the shell)
   Account level and XP, gems, the cross-game coin bank, daily missions, the
   daily login streak and the per-game stats the home screen shows on each
   tile. Games talk to it only through `pa_meta_report` / `pa_meta_run_end`
   in pa.h; everything here is for hub.c.

   Persisted through pa_save_* under `meta.*` keys. In review captures
   (pa_demo_mode() != 0) the state is seeded in memory and never written, so a
   capture can never touch a real player's save.
   =========================================================================== */
#ifndef PA_META_H
#define PA_META_H

#include "pa.h"

#define META_MAX_GAMES 16
#define META_MISSIONS  3
#define META_STREAK_CYCLE 7

typedef enum {
    MIS_RUNS,        /* play N runs of anything */
    MIS_COINS,       /* bank N coins across games */
    MIS_GAMES,       /* play N different games */
    MIS_GAME_RUNS,   /* play one named game N times */
    MIS_WINS,        /* win or clear N levels */
    MIS_BEST,        /* beat a personal best N times */
    MIS_TYPE_COUNT
} MetaMissionType;

typedef enum { MIS_ACTIVE = 0, MIS_DONE = 1, MIS_CLAIMED = 2 } MetaMissionState;

typedef struct {
    int  type, game;           /* game: index for MIS_GAME_RUNS, else -1 */
    int  target, progress, state, reward;
    char label[64];
} MetaMission;

typedef struct {
    int best;     /* best reported score */
    int runs;     /* runs reported */
    int xp;       /* account XP earned in this game: drives its mastery stars */
    int level;    /* highest level reported (level-based games), 0 if none */
} MetaGameStats;

/** What happened since the hub last looked: drives the return-home rewards. */
typedef struct {
    int xp;            /* account XP gained */
    int coins;         /* coins banked */
    int level_from;    /* account level before */
    int level_to;      /* account level now */
    int level_gems;    /* gems paid for those level-ups (already in the bank) */
    int missions_done; /* missions that became claimable */
    int runs;
} MetaPending;

typedef struct {
    int level, xp, gems, coins;
    int day;             /* local civil day number */
    int secs_to_reset;   /* until local midnight: the missions timer */
    int streak;          /* consecutive days opened, 1-based */
    int streak_claimed;  /* today's login reward has been taken */
    int featured;        /* game index with 2x XP today */
    MetaMission missions[META_MISSIONS];
    int bonus_state;     /* all-three chest: MIS_ACTIVE / MIS_DONE / MIS_CLAIMED */
    int bonus_reward;
    int played_mask;     /* games played today, for MIS_GAMES */
    MetaGameStats game[META_MAX_GAMES];
    int game_count;
} MetaState;

/** `games` is the hub's table; indices in MetaState refer to it. */
void meta_init(const PA_Game *const *games, int count);
/** Re-reads the clock; rolls missions and the streak over at midnight. */
void meta_refresh_clock(void);
const MetaState *meta_state(void);

int  meta_xp_need(int level);
int  meta_total_xp(void);
/** Splits a lifetime XP total into level, XP into that level and its need. */
void meta_level_from_total(int total, int *level, int *into, int *need);
int  meta_levelup_gems(int new_level);
int  meta_streak_reward(int streak_day);   /* 1-based day in the 7-day cycle */
int  meta_streak_cycle_day(void);          /* today's 1..7 slot */
int  meta_game_stars(int game);            /* 0..3 mastery */
/** "LEVEL 12", "BEST 4,210" or "" for an unplayed game. */
void meta_game_progress(int game, char *buf, size_t n);
int  meta_claimable(void);                 /* missions + chest ready to claim */

void meta_take_pending(MetaPending *out);
int  meta_claim_streak(void);              /* returns gems granted (0 if none) */
int  meta_claim_mission(int i);
int  meta_claim_bonus(void);

void pa_fmt_int(int v, char *buf, size_t n);  /* 12,345 */

#endif
