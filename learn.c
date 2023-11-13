/*
	the main file to "play" two models against each other

	arguments are <first model.db> <second model.db>
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>
#include <time.h>

#include "structs.h"
#include "model.h"

static int _play = 1;

void _handle_signal(int sig)
{
	_play = 0;
}

int main(int argc, char**argv)
{
	if(argc != 3)
	{
		fprintf(stderr, "usage: %s <model1> <model2>\n", *argv);
		return 1;
	}
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = _handle_signal;
	if(sigaction(SIGINT, &sa, NULL))
	{
		fprintf(stderr, "failed to setup signal handler\n");
		return 1;
	}
	// 1. init models/players
	void * models[2];
	for(int i=0;i<2;i++)
	{
		void *md = init_model(argv[i+1]);
		if(!md)
			return 1;
		models[i] = md;
	}
	int counters[2] = {0,0};
	int swapped = 0;
	srand(time(NULL));
	// 2. while true, play game
	while(_play)
	{
		// setup game
		struct state_key skey = {0, 0};
		struct move move;
		skey.to_move = 1;
		// 18 place moves
		for(int i=0; i<18 && _play; i++, skey.to_move ^= 3)
		{
			if(model_play(models[i&1], skey, 0, &move))
				goto _err;
			// do the move
			assert(move.source == 0);
			//fprintf(stderr, "%d: %d %d (%d)\n", skey.to_move, move.source, move.dest, move.remove);
			{
				int dest = move.dest;
				skey.board |= ((uint64_t)skey.to_move) << (dest+dest-2);
			}
			if(move.remove)
			{
				int rem = move.remove;
				long p_b = skey.board;
				skey.board &= 0xffffffffffff ^ (3UL << (rem+rem-2));
				//printf("[REMOVE]\nprev: %12lx\nnow:  %12lx\n", p_b, skey.board);
			}
			//print_board(skey);
		}

		//printf("starting slides (_play=%d)\n", _play);

		// slide moves to the infinity
		int stalemate = 0;
		while(_play)
		{
			{
				// check player count
				int p1_count = 0;
				int p2_count = 0;
				long _board = skey.board;
				for(int i=0;i<24;i++)
				{
					int _piece = _board & 3;
					if(_piece == 1)
						p1_count ++;
					else if(_piece == 2)
						p2_count ++;
					_board >>= 2;
				}
				if(p1_count < 3)
					goto _p2_win;
				if(p2_count < 3)
					goto _p1_win;
				// check 3-3 state for over 9 moves (stalemate)
				if(p1_count == p2_count && p1_count == 3)
					stalemate ++;
				if(stalemate == 9)
					goto _stalemate;
			}

			if(model_play(models[skey.to_move-1], skey, 1, &move))
			{
				printf("error?\n");
				goto _err;
			}
			if(move.dest == 0)
			{
				// can't move, loses
				if(skey.to_move == 1)
					goto _p2_win;
				goto _p1_win;
			}
			//fprintf(stderr, "%d: %d %d (%d)\n", skey.to_move, move.source, move.dest, move.remove);

			// remove source
			{
				int src = move.source;
				skey.board &= 0xffffffffffff ^ (3UL << (src+src-2));
			}
			// set dest
			{
				int dest = move.dest;
				skey.board |= ((uint64_t)skey.to_move) << (dest+dest-2);
			}
			// remove remove
			if(move.remove)
			{
				int remove = move.remove;
				skey.board &= 0xffffffffffff ^ (3UL << (remove+remove-2));
			}
			//print_board(skey);


			skey.to_move ^= 3;
		}
		if(!_play)
			break;

		// 3. notify winner
	_p1_win:
		puts("p1 wins");
		if(model_result(models[0], 1) || model_result(models[1], 0))
			goto _err;
		counters[0] ++;
		goto _cont;
	_p2_win:
		puts("p2 wins");
		if(model_result(models[1], 1) || model_result(models[0], 0))
			goto _err;
		counters[1] ++;
		goto _cont;
	_stalemate:
		puts("stalemate");
		if(model_result(models[0], 0) || model_result(models[1], 0))
			goto _err;
	_cont:

		// 4. swap models and counters
		{
			void *p = models[0];
			models[0] = models[1];
			models[1] = p;
		}
		// fuck you
		//models[0] ^= models[1];
		//models[1] ^= models[0];
		//models[0] ^= models[1];
		counters[0] ^= counters[1];
		counters[1] ^= counters[0];
		counters[0] ^= counters[1];
		swapped ^= 1;

		// print result
		printf("current result: %d = %d\n", counters[0 ^ swapped], counters[1 ^ swapped]);
	}
	// finish by sigint
_err:
	end_model(models[0]);
	end_model(models[1]);

	return 0;
}
