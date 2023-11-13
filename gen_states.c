// -l lmdb

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <lmdb.h>

#include "structs.h"

#define DEFAULT_DB_LOC "states.db"

// struct for shared data
struct sdata
{
	MDB_txn *db_txn;
	MDB_dbi dbi;
	void *buffer;
	struct state *state;
	struct state_key *state_key;
	struct move *place_ptr;
	struct move *slide_ptr;
	struct move *next_ptr;
	uint8_t p1_count;
	uint8_t p1_places[9];	// that's maximum
	uint8_t p2_count;
	uint8_t p2_places[9];
};

static int gen_piece_count(struct sdata *sdata);
static int gen_1_placement(struct sdata *sdata);
static int gen_2_placement(struct sdata *sdata);
static int gen_movements(struct sdata *sdata);


int main(int argc, char **argv)
{
	int r;
	char *db_loc = DEFAULT_DB_LOC;
	if(argc > 1)
		db_loc = argv[1];
	// 0. initialize
	MDB_env *db_env = NULL;
	if((r = mdb_env_create(&db_env)))
	{
		fprintf(stderr, "error creating environment: %s\n", mdb_strerror(r));
		return r;
	}
	if((r = mdb_env_open(db_env, db_loc, MDB_NOSUBDIR|MDB_NOLOCK, 0644)))
	{
		fprintf(stderr, "error opening environment: %s\n", mdb_strerror(r));
		return r;
	}

	MDB_txn *txn;
	if((r = mdb_txn_begin(db_env, NULL, 0, &txn)))
	{
		fprintf(stderr, "error starting transaction: %s\n", mdb_strerror(r));
		return r;
	}
	MDB_dbi dbi;
	if((r = mdb_dbi_open(txn, NULL, MDB_INTEGERKEY, &dbi)))
	{
		fprintf(stderr, "error creating database: %s\n", mdb_strerror(r));
		return r;
	}

	// shared buffer
	void *buffer = malloc(sizeof(struct state) + 512*sizeof(struct move));
	if(!buffer)
	{
		fprintf(stderr, "error allocating memory\n");
		return 1;
	}
	struct sdata sdata;
	sdata.db_txn = txn;
	sdata.dbi = dbi;
	sdata.buffer = buffer;
	sdata.state = (struct state*)buffer;
	sdata.state_key = &sdata.state->state_key;
	// rest will be overriden

	r = gen_piece_count(&sdata);
	if(r)
		return r;

	// close txn and dbi

	if((r = mdb_txn_commit(txn)))
	{
		fprintf(stderr, "failed to commit transaction: %s\n", mdb_strerror(r));
		return r;
	}
	mdb_dbi_close(db_env, dbi);
	mdb_env_close(db_env);

	return r;

	// 1. for pieces in gen_pieces
	// 2. for 1st player placement in placements
	// 3. for 2nd player placement in placements
	// the above is just for the key
	// 4. gen all moves
}

static int gen_piece_count(struct sdata *sdata)
{
	int s = 0;
	while(s < 100)
	{
		div_t d = div(s, 10);
		sdata->p1_count = d.quot;
		sdata->p2_count = d.rem;

		fprintf(stderr, "doing %d v %d\n", d.quot, d.rem);

		if(gen_1_placement(sdata))
			return 1;
		s += 1;
	}
	return 0;
}

static int gen_1_placement(struct sdata *sdata)
{
	// pool is [idx] -> idx + 1;
	int r = sdata->p1_count;
	if(!r)
	{
		// special case, only 1: empty
		return gen_2_placement(sdata);
	}
	int *indices = (int*)alloca(r*sizeof(int));
	for(int i=0;i<r;i++)
		indices[i] = i;

	do
	{
		// yield
		{
			for(int j=0;j<r;j++)
				sdata->p1_places[j] = indices[j]+1;
			if(gen_2_placement(sdata))
				return 1;
		}
		int i;
		for(i = r-1; i>=0; i--)
		{
			if(indices[i] != (i+24-r))
				goto _break;
		}
		// else
		return 0;
	_break:
		indices[i] += 1;
		for(int j=i+1; j<r; j++)
			indices[j] = indices[j-1] + 1;
	}
	while(1);

	return 0;
}

static int gen_2_placement(struct sdata *sdata)
{
	// the pool here is the places not taken yet
	int r = sdata->p2_count;
	int pool_len = 24-sdata->p1_count;
	int *pool = (int*)alloca(pool_len*sizeof(int));
	for(int i=0,j=0;i<24 && j<pool_len; i++)
	{
		int p = i+1;
		for(int l=0;l<sdata->p1_count;l++)
		{
			if(sdata->p1_places[l] == p)
				goto _break1;
		}
		// else, it isn't taken yet
		pool[j++] = p;
	_break1: ;
	}

	int *indices = (int*)alloca(r*sizeof(int));
	for(int i=0;i<r;i++)
		indices[i] = i;

	do
	{
		// yield
		{
			for(int j=0;j<r;j++)
				sdata->p2_places[j] = pool[indices[j]];
			if(gen_movements(sdata))
				return 1;
		}
		int i;
		for(i = r-1; i>=0; i--)
			if(indices[i] != (i+24-r))
				goto _break2;
		return 0;
	_break2:
		indices[i] += 1;
		for(int j=i+1; j<r; j++)
			indices[j] = indices[j-1]+1;
	}
	while(1);

	return 0;
}

static int _is_inline(uint8_t *board, int player_number, int place)
{
	struct lines lines = board_lines[place];
	for(int l=0;l<2;l++)
	{
		struct line line = lines.line[l];
		for(int p=0;p<3;p++)
			if(board[line.n[p]] != player_number)
				goto _no_line;
		// else, line
		return 1;
	_no_line: ;
	}
	// else, no line
	return 0;
}

// populate moves
static void _do_move(struct sdata *sdata, uint8_t *move_counter, uint8_t *board, int to_move, int source, int dest)
{
	int place = dest;
	int other = to_move ^ 3;
	int other_pieces_count = to_move == 1 ? sdata->p2_count : sdata->p1_count;
	uint8_t *other_pieces = to_move == 1 ? sdata->p2_places : sdata->p1_places;

	int prev_source = 0;
	if(source)
	{
		prev_source = board[source];
		board[source] = 0;
	}
	board[place] = to_move;

	int removed = 0;
	if(_is_inline(board, to_move, place))
	{
		// do remove aswell
		// check all other opponents
		for(int i=0;i<other_pieces_count;i++)
		{
			int other_place = other_pieces[i];
			if(!_is_inline(board, other, other_place))
			{
				// remove it
				sdata->next_ptr->source = source;
				sdata->next_ptr->dest = place;
				sdata->next_ptr->remove = other_place;
				sdata->next_ptr->weight = 1;
				(*move_counter)++;
				sdata->next_ptr++;
				removed++;
			}
			// else, skip it
		}
	}

	// else
	if(!removed)
	{
		// either it was not inline, or there's no piece to remove
		// do normal movement
		sdata->next_ptr->source = source;
		sdata->next_ptr->dest = place;
		sdata->next_ptr->remove = 0;
		sdata->next_ptr->weight = 1;
		(*move_counter)++;
		sdata->next_ptr++;
	}

	// restore board
	board[place] = 0;
	if(source)
		board[source] = prev_source;
}

static int gen_movements(struct sdata *sdata)
{
	// faster way to check if there are pieces somewhere
	uint8_t board[25];
	memset(board, 0, 25);
	for(int i=0;i<sdata->p1_count;i++)
		board[sdata->p1_places[i]] = 1;
	for(int i=0;i<sdata->p2_count;i++)
		board[sdata->p2_places[i]] = 2;

	for(int to_move=1; to_move<=2; to_move++)
	{
		// which to move?
		int other = to_move ^ 3;
		int my_pieces_count = to_move == 1 ? sdata->p1_count : sdata->p2_count;
		uint8_t *my_pieces = to_move == 1 ? sdata->p1_places : sdata->p2_places;
		int other_pieces_count = to_move == 1 ? sdata->p2_count : sdata->p1_count;
		uint8_t *other_pieces = to_move == 1 ? sdata->p2_places : sdata->p2_places;

		sdata->state->place_moves = 0;
		sdata->state->slide_moves = 0;
		sdata->place_ptr = (struct move*)(sdata->buffer + sizeof(struct state));
		sdata->next_ptr = sdata->place_ptr;

		// place moves
		for(int place=1;place<=24;place++)
		{
			if(board[place])
				// already there
				continue;
			_do_move(sdata, &sdata->state->place_moves, board, to_move, 0, place);
		}
		if(my_pieces_count < 3)
			// no slide, either place or already dead
			goto _no_slide;
		// slide moves
		for(int i=0;i<my_pieces_count;i++)
		{
			int source = my_pieces[i];
			if(my_pieces_count == 3)
			{
				// we can move it anywhere
				for(int dest=1;dest<=24;dest++)
				{
					if(board[dest])
						continue;
					_do_move(sdata, &sdata->state->slide_moves, board, to_move, source, dest);
				}
			}
			else	// just slide
			{
				// check all dests
				for(uint8_t *nei = board_moves[source]; *nei; nei++)
				{
					if(board[*nei])
						// can't do that one
						continue;
					// else, do it
					_do_move(sdata, &sdata->state->slide_moves, board, to_move, source, *nei);
				}
			}
		}
	_no_slide:

		// now insert it into database
		// build key
		sdata->state_key->board = 0;
		sdata->state_key->_pad = 0;
		sdata->state_key->to_move = to_move;
		for(int i=1;i<=24;i++)
			sdata->state_key->board |= (board[i]&3) << (i+i-2);
		// prepare key and value
		{
			MDB_val key, data;
			key.mv_size = sizeof(struct state_key);
			key.mv_data = sdata->state_key;
			data.mv_size = ((void*)sdata->next_ptr) - sdata->buffer;	// total occupied space
			data.mv_data = sdata->buffer;
		}
	}

	// all good
	return 0;
}
