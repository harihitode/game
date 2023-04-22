#include <curses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __MACH__
#include <pthread.h>
typedef pthread_t thrd_t;
typedef pthread_mutex_t mtx_t;
enum { thrd_error = 0, thrd_success = 1 };
enum { mtx_plain = 0, mtx_recursive = 1, mtx_timed = 2 };
typedef void *(*thrd_start_t)(void *);
inline int thrd_create(thrd_t *thr, thrd_start_t start_routine, void *arg) {
  if (pthread_create(thr, NULL, start_routine, arg) == 0) {
    return thrd_success;
  } else {
    return thrd_error;
  }
}
_Noreturn void thrd_exit(int res) { pthread_exit((void *)(intptr_t)res); }
inline int thrd_join(thrd_t thr, int *res) {
  void *pres;
  if (pthread_join(thr, &pres) != 0) {
    return thrd_error;
  }
  if (res != NULL) {
    *res = (int)(intptr_t)pres;
  }
  return thrd_success;
}
inline int mtx_init(mtx_t *mtx, int type) {
  if ((type == mtx_plain) && (pthread_mutex_init(mtx, NULL) == 0)) {
    return thrd_success;
  } else {
    return thrd_error;
  }
}
inline int mtx_lock(mtx_t *mtx) {
  if (pthread_mutex_lock(mtx) == 0) {
    return thrd_success;
  } else {
    return thrd_error;
  }
}
inline int mtx_unlock(mtx_t *mtx) {
  if (pthread_mutex_unlock(mtx) == 0) {
    return thrd_success;
  } else {
    return thrd_error;
  }
}
inline int mtx_destroy(mtx_t *mtx) {
  if (pthread_mutex_destroy(mtx) == 0) {
    return thrd_success;
  } else {
    return thrd_error;
  }
}
#else
#include <threads.h>
#endif

#define NONE 0
#define SELF 1
#define ENEMY 2
#define BULLET 3
#define EXPLODE 4
#define OPTION 5

#define RED 1
#define YELLOW 2
#define GREEN 3

#define STATE_PLAYING 0
#define STATE_POSING 1
#define STATE_FINISH_REQ 2
#define STATE_FINISH 3

struct context {
  WINDOW *wnd;
  int window_col;
  int window_row;
  mtx_t mtx;
  struct entity_list *ent;
  int state;
  int score;
  int options;
  int beam;
};

struct entity_list {
  int num_entity;
  // components
  char *val;
  int *self;
  int *col;
  int *row;
  int *ttl;
  void (**age)(struct context *ctx, int i);
};

int randgen(int min, int max) {
  return min + (int)(rand() * (max - min + 1.0) / (1.0 + RAND_MAX));
}

void init_entity(struct entity_list *elist) {
  elist->num_entity = 0;
  elist->val = NULL;
  elist->self = NULL;
  elist->col = NULL;
  elist->row = NULL;
  elist->ttl = NULL;
  elist->age = NULL;
}

int entity_alloc(struct entity_list *elist) {
  int found = -1;
  for (int i = 0; i < elist->num_entity; i++) {
    if (elist->ttl[i] == 0) {
      found = i;
      break;
    }
  }
  if (found >= 0) {
    return found;
  } else {
    int ret = elist->num_entity;
    elist->val = (char *)realloc(elist->val, (ret + 1) * sizeof(char));
    elist->self = (int *)realloc(elist->self, (ret + 1) * sizeof(int));
    elist->col = (int *)realloc(elist->col, (ret + 1) * sizeof(int));
    elist->row = (int *)realloc(elist->row, (ret + 1) * sizeof(int));
    elist->ttl = (int *)realloc(elist->ttl, (ret + 1) * sizeof(int));
    elist->age = (void (**)(struct context *, int))realloc(
        elist->age, (ret + 1) * sizeof(void *));
    elist->num_entity++;
    return ret;
  }
}

void fini_entity(struct entity_list *elist) {
  free(elist->val);
  free(elist->self);
  free(elist->col);
  free(elist->row);
  free(elist->ttl);
}

void init_context(struct context *ctx) {
  ctx->wnd = initscr(); // initialize the window
  cbreak();             // no waiting for enter key
  noecho();             // no echoing
  curs_set(0);          // no cursor
  getmaxyx(ctx->wnd, ctx->window_row,
           ctx->window_col); // calling to find size of window
  ctx->ent = NULL;
  mtx_init(&ctx->mtx, mtx_plain);
  ctx->state = STATE_POSING;
  ctx->score = 0;
  ctx->options = 0;
  ctx->beam = 0;
  clear();
}

void fini_context(struct context *ctx) {
  endwin();
  mtx_destroy(&ctx->mtx);
}

void forward(struct context *ctx, int i) {
  if (ctx->ent->row[i] >= 0 && ctx->ent->row[i] <= ctx->window_row) {
    ctx->ent->row[i]--;
  } else {
    ctx->ent->ttl[i] = 0;
  }
}

void backward(struct context *ctx, int i) {
  if (ctx->ent->row[i] >= 0 && ctx->ent->row[i] <= ctx->window_row) {
    ctx->ent->row[i]++;
  } else {
    ctx->ent->ttl[i] = 0;
  }
}

void stay(struct context *ctx, int i) {
  if (ctx->ent->ttl[i] > 0)
    ctx->ent->ttl[i]--;
}

void aging(struct context *ctx) {
  for (int i = 0; i < ctx->ent->num_entity; i++) {
    if (ctx->ent->age[i] != NULL) {
      ctx->ent->age[i](ctx, i);
    }
  }
}

int isgameover(struct context *ctx) {
  if (ctx->ent->ttl[0] == 0 || ctx->state == STATE_FINISH_REQ) {
    return 1;
  } else {
    return 0;
  }
}

int hit(struct context *ctx) {
  for (int i = 0; i < ctx->ent->num_entity; i++) {
    int source_type = ctx->ent->self[i];
    if (source_type != BULLET && source_type != SELF) {
      continue;
    }
    int col = ctx->ent->col[i];
    int row = ctx->ent->row[i];
    for (int j = 0; j < ctx->ent->num_entity; j++) {
      int target_type = ctx->ent->self[j];
      if (ctx->ent->col[j] == col &&
          (ctx->ent->row[j] == row || (ctx->ent->row[j] - 1) == row)) {
        if (source_type == BULLET && target_type == ENEMY) {
          ctx->score += 100;
          // delete enemy
          ctx->ent->val[j] = '#';
          ctx->ent->self[j] = EXPLODE;
          ctx->ent->ttl[j] = 5;
          ctx->ent->age[j] = stay;
          // delete bullet
          ctx->ent->ttl[i] = 0;
          ctx->ent->age[i] = stay;
        } else if (source_type == SELF && target_type == ENEMY) {
          ctx->ent->ttl[i] = 0;
        } else if (source_type == SELF && target_type == OPTION) {
          // delete target
          ctx->ent->self[j] = NONE;
          ctx->ent->ttl[j] = 0;
          ctx->options++;
        }
      }
    }
  }
  return 0;
}

int draw(struct context *ctx) {
  char score_str[128];
  const char *pose_str = "press space to start";
  int pose = (ctx->state == STATE_POSING) ? 1 : 0;
  start_color(); // color
  use_default_colors();
  init_pair(RED, COLOR_RED, -1);
  init_pair(YELLOW, COLOR_YELLOW, -1);
  init_pair(GREEN, COLOR_GREEN, -1);
  while (1) {
    erase();
    mtx_lock(&ctx->mtx);
    aging(ctx);
    if (pose == 1 && ctx->state == STATE_PLAYING) {
      // awake enemies
      for (int i = 0; i < ctx->ent->num_entity; i++) {
        if (ctx->ent->self[i] == ENEMY) {
          ctx->ent->age[i] = backward;
        }
      }
      pose = 0;
    }

    if (pose == 0 && ctx->state == STATE_POSING) {
      // sleep enemies
      for (int i = 0; i < ctx->ent->num_entity; i++) {
        if (ctx->ent->self[i] == ENEMY) {
          ctx->ent->age[i] = NULL;
        }
      }
      pose = 1;
    }

    if (ctx->state == STATE_PLAYING) {
      // gen enemies
      if (randgen(0, 9) > 1) {
        int i = entity_alloc(ctx->ent);
        if (randgen(0, 9) > 8) {
          ctx->ent->self[i] = OPTION;
          ctx->ent->val[i] = '+';
        } else {
          ctx->ent->self[i] = ENEMY;
          ctx->ent->val[i] = randgen((int)'a', (int)'z');
        }
        ctx->ent->row[i] = 0;
        ctx->ent->col[i] = randgen(0, ctx->window_col);
        ctx->ent->age[i] = backward;
        ctx->ent->ttl[i] = -1;
      }
    }

    // gen beams
    if (ctx->beam == 1) {
      for (int b = 0; b <= ctx->options; b++) {
        int i = entity_alloc(ctx->ent);
        ctx->ent->self[i] = BULLET;
        ctx->ent->val[i] = '|';
        ctx->ent->row[i] = ctx->ent->row[0] - 1;
        ctx->ent->age[i] = forward;
        ctx->ent->ttl[i] = -1;
        if ((b & 1) == 1) {
          ctx->ent->col[i] = ctx->ent->col[0] + ((b + 1) / 2);
        } else {
          ctx->ent->col[i] = ctx->ent->col[0] - (b / 2);
        }
      }
      ctx->beam = 0;
    }
    hit(ctx);
    // draw
    if (ctx->state == STATE_POSING) {
      mvaddstr(ctx->window_row * 2 / 3,
               ctx->window_col / 2 - strlen(pose_str) / 2, pose_str);
    }
    for (int i = 0; i < ctx->ent->num_entity; i++) {
      if (ctx->ent->self[i] != NONE && ctx->ent->ttl[i] != 0) {
        if (ctx->ent->self[i] == SELF) {
          attron(COLOR_PAIR(RED));
        } else if (ctx->ent->self[i] == OPTION) {
          attron(COLOR_PAIR(GREEN));
        } else if (ctx->ent->self[i] == EXPLODE) {
          attron(COLOR_PAIR(YELLOW));
        }
        mvaddch(ctx->ent->row[i], ctx->ent->col[i], ctx->ent->val[i]);
        if (ctx->ent->self[i] == SELF) {
          attroff(COLOR_PAIR(RED));
        } else if (ctx->ent->self[i] == OPTION) {
          attroff(COLOR_PAIR(GREEN));
        } else if (ctx->ent->self[i] == EXPLODE) {
          attroff(COLOR_PAIR(YELLOW));
        }
      }
    }
    mtx_unlock(&ctx->mtx);
    if (ctx->state == STATE_PLAYING) {
      ctx->score++;
    }
    sprintf(score_str, "SCORE: %d OPTIONS: %d\n", ctx->score, ctx->options);
    mvaddstr(0, 0, score_str);
    refresh();
    if (isgameover(ctx)) {
      break;
    }
    usleep(100 * 1000);
  }
  sprintf(score_str, "GAME OVER");
  mvaddstr(ctx->window_row * 2 / 3, ctx->window_col / 2 - strlen(score_str) / 2,
           score_str);
  sprintf(score_str, "press any key");
  mvaddstr(ctx->window_row * 2 / 3 + 1,
           ctx->window_col / 2 - strlen(score_str) / 2, score_str);
  refresh();
  ctx->state = STATE_FINISH;
  thrd_exit(0);
}

int main() {
  const char *init_str_1 =
      "ICONS: @ = YOU; + = power-up item; alphabet = enemy";
  const char *init_str_2 =
      "KEYS: h/j/k/l = UP/LEFT/RIGHT/DOWN; SPACE = BEAM; ESC to quit";
  const char *init_strs[2];
  init_strs[0] = init_str_1;
  init_strs[1] = init_str_2;
  struct context *ctx = (struct context *)malloc(1 * sizeof(struct context));
  struct entity_list *ent =
      (struct entity_list *)malloc(1 * sizeof(struct entity_list));
  srand((unsigned int)time(NULL));
  init_entity(ent);
  init_context(ctx);

  ctx->ent = ent;

  entity_alloc(ctx->ent);
  ctx->ent->self[0] = SELF;
  ctx->ent->val[0] = '@';
  ctx->ent->row[0] = ctx->window_row / 2;
  ctx->ent->col[0] = ctx->window_col / 2;
  ctx->ent->age[0] = NULL;
  ctx->ent->ttl[0] = -1;

  for (unsigned s = 0; s < 2; s++) {
    for (unsigned i = 0; i < strlen(init_strs[s]); i++) {
      int n = entity_alloc(ctx->ent);
      ctx->ent->self[n] = ENEMY;
      ctx->ent->val[n] = init_strs[s][i];
      ctx->ent->row[n] = 1 + s;
      ctx->ent->col[n] = (ctx->window_col / 2) - (strlen(init_strs[s]) / 2) + i;
      ctx->ent->age[n] = NULL;
      ctx->ent->ttl[n] = -1;
    }
  }

  thrd_t t;
  thrd_create(&t, (thrd_start_t)draw, ctx);

  while (1) {
    __sync_synchronize();
    char c = getch();
    if (c != 0) {
      mtx_lock(&ctx->mtx);
      switch (c) {
      case 'h': // LEFT
        ent->col[0]--;
        break;
      case 'j': // DOWN
        ent->row[0]++;
        break;
      case 'k': // UP
        ent->row[0]--;
        break;
      case 'l': // RIGHT
        ent->col[0]++;
        break;
      case '+': // CHEAT! You are bad!
        ctx->options++;
        break;
      case ' ':
        if (ctx->state == STATE_POSING) {
          ctx->state = STATE_PLAYING;
        } else {
          ctx->beam = 1;
        }
        break;
      case 27:
        ent->ttl[0] = 0;
        ctx->state = STATE_FINISH_REQ;
        break;
      default:
        break;
      }
      mtx_unlock(&ctx->mtx);
    }
    if (ctx->state == STATE_FINISH)
      break;
  }
  thrd_join(t, NULL);

  int total_score = ctx->score;
  fini_entity(ent);
  free(ent);
  fini_context(ctx);
  free(ctx);
  printf("SCORE: %d\n", total_score);
  return 0;
}
