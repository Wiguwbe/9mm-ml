#ifndef _9MM_MODEL_H
#define _9MM_MODEL_H

#include "structs.h"

// model returns private model data
void *
init_model(char *model_path);

int end_model(void *model_data);

// model returns a move
int model_play(void *model_data, struct state_key state_key, int slide, struct move *out);

// to reinforce/penalize
int model_result(void *model_data, int win);

#endif
