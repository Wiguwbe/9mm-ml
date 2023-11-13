#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include <unqlite.h>

#include "model.h"

struct move_list {
	struct move_list *next;
	struct state_key state_key;
	struct move move;
};

struct model_data {
	unqlite *db;
	void *buffer;
	struct state *state;
	struct state_key *state_key;
	struct move *next_ptr;
	uint8_t p1_count;
	uint8_t p1_places[9];
	uint8_t p2_count;
	uint8_t p2_places[9];
	struct move_list ml_head, *ml_tail;
};

static const long buffer_size = (
	sizeof(struct model_data)
	+ sizeof(struct state)
	+ 512 * sizeof(struct move)
);

static int gen_movements(struct model_data *md, struct state_key state_key);

void *init_model(char *model_path)
{
	// allocate model data and buffer already
	void *raw = malloc(buffer_size);
	if(!raw)
		return NULL;
	struct model_data *md = (struct model_data*)raw;
	md->buffer = raw+sizeof(struct model_data);
	md->state = (struct state*)md->buffer;
	md->state_key = &md->state->state_key;
	md->ml_tail = &md->ml_head;
	md->ml_head.next = NULL;

	// open db
	if(unqlite_open(&md->db, model_path, UNQLITE_OPEN_CREATE|UNQLITE_OPEN_OMIT_JOURNALING|UNQLITE_OPEN_NOMUTEX) != UNQLITE_OK)
	{
		free(md);
		return NULL;
	}
	return raw;
}

int end_model(void *model_data)
{
	struct model_data *md = (struct model_data*)model_data;
	model_result(model_data, 0);
	unqlite_close(md->db);
	free(model_data);
	return 0;
}

int model_play(void *model_data, struct state_key state_key, int slide, struct move *out)
{
	struct model_data *md = (struct model_data*)model_data;
	int rc;

	md->state->state_key = state_key;

	// read key from database
	unqlite_int64 bufsize = buffer_size;
	switch((rc=unqlite_kv_fetch(md->db, &state_key, sizeof(state_key), md->buffer, &bufsize)))
	{
	case UNQLITE_OK:
		// good
		break;
	case UNQLITE_NOTFOUND:
		// generate it
		if(gen_movements(md, state_key) == 0)
			break;
		// else, fallthrough
	default:
		fprintf(stderr, "failed to fetch/generate move\n");
		return 1;
	}

	// pick a random move
	{
		// get total weight
		int total_weight = 0;
		struct move *move_ptr = (struct move*)(md->buffer + sizeof(struct state));
		if(slide)
			move_ptr += md->state->place_moves;
		int move_counter = slide ? md->state->slide_moves : md->state->place_moves;
		if(move_counter == 0)
		{
			// can't move, loses
			//assert(0); // for debugging/core
			out->source = 0;
			out->dest = 0;
			out->remove = 0;
			return 0;
		}

		struct move *ptr = move_ptr;
		for(int i=0;i<move_counter;i++,ptr++)
			total_weight += ptr->weight;

		//fprintf(stderr, "moves: %d %d\n", md->state->place_moves, md->state->slide_moves);

		// random value up to total_weight
		int rvalue = random() % total_weight;
		// now pick the move
		ptr = move_ptr;
		while(rvalue >= ptr->weight)
		{
			rvalue -= ptr->weight;
			ptr++;
		}
		memcpy(out, ptr, sizeof(struct move));

		// store it
		struct move_list *ml = (struct move_list*)malloc(sizeof(struct move_list));
		if(!ml)
		{
			fprintf(stderr, "failed to allocate memory\n");
			return 1;
		}
		md->ml_tail->next = ml;
		md->ml_tail = ml;
		ml->next = NULL;
		ml->state_key = state_key;
		memcpy(&ml->move, ptr, sizeof(struct move));
	}
	// return it
	return 0;
}

int model_result(void *model_data, int win)
{
	struct model_data *md = (struct model_data*)model_data;
	int r = 0;
	int rc;

	// iterate, free and update weight
	struct move_list *ptr, *next;
	ptr = md->ml_head.next;
	while(ptr)
	{
		next = ptr->next;
		if(win && !r)
		{
			// update kv
			// fetch
			unqlite_int64 buf_size = buffer_size;
			if((rc=unqlite_kv_fetch(md->db, &ptr->state_key, sizeof(ptr->state_key), md->buffer, &buf_size)) != UNQLITE_OK)
			{
				fprintf(stderr, "failed to fetch previous state\n", rc);
				r = 1;
				goto _free;
			}
			// look for move
			struct move *m_ptr = (struct move*)(md->buffer+sizeof(struct state));
			int total_moves = (int)md->state->place_moves + (int)md->state->slide_moves;
			//printf("total_moves=%d\n",total_moves);
			while(total_moves--)
			{
				//printf("target:  %d %d %d\n", ptr->move.source, ptr->move.dest, ptr->move.remove);
				//printf("current: %d %d %d (%p)\n", m_ptr->source, m_ptr->dest, m_ptr->remove, m_ptr);
				if(m_ptr->source == ptr->move.source && m_ptr->dest == ptr->move.dest && m_ptr->remove == ptr->move.remove)
				{
					// that's it
					goto _found_it;
				}
				m_ptr ++;
			}
			// else
			fprintf(stderr, "failed to find previous move\n");
			r = 1;
			goto _free;
		_found_it:
			// update it
			m_ptr->weight ++;
			// and store it
			if(unqlite_kv_store(md->db, &ptr->state_key, sizeof(ptr->state_key), md->buffer, buf_size) != UNQLITE_OK)
			{
				fprintf(stderr, "failed to update move\n");
				r = 1;
				goto _free;
			}
			if(unqlite_commit(md->db))
			{
				fprintf(stderr, "failed to commit store\n");
				r = 1;
				goto _free;
			}
		}
	_free:
		free(ptr);
		ptr = next;
	}

	// restore initial state
	md->ml_tail = &md->ml_head;
	return r;
}

struct gm_data {
	int to_move;
	int other;
	int my_piece_count;
	int other_piece_count;
	uint8_t *my_places;
	uint8_t *other_places;
	uint8_t *board;
};

static int _is_inline(uint8_t *board, int pl_number, int place)
{
	struct lines lines = board_lines[place];
	for(int l=0;l<2;l++)
	{
		struct line line = lines.line[l];
		for(int p=0;p<3;p++)
		{
			if(board[line.n[p]] != pl_number)
				goto _no_line;
		}
		// else, line
		return 1;
	_no_line: ; // try next
	}
	// no line
	return 0;
}

static void _do_move(struct model_data *md, uint8_t *move_counter, struct gm_data *gm, int source, int dest)
{
	int prev_source = 0;
	if(source)
	{
		prev_source = gm->board[source];
		gm->board[source] = 0;
	}
	gm->board[dest] = gm->to_move;

	int removed = 0;
	if(_is_inline(gm->board, gm->to_move, dest))
	{
		// do remove
		for(int i=0;i<gm->other_piece_count;i++)
		{
			int other_place = gm->other_places[i];
			if(gm->board[other_place] != gm->other)
				// not reachable
				continue;
			if(!_is_inline(gm->board, gm->other, other_place))
			{
				// remove it
				md->next_ptr->source = source;
				md->next_ptr->dest = dest;
				md->next_ptr->remove = other_place;
				md->next_ptr->weight = 1;
				*move_counter += 1;
				md->next_ptr ++;
				removed++;
			}
		}
	}

	// else
	if(!removed)
	{
		// either not inline or no piece to remove
		md->next_ptr->source = source;
		md->next_ptr->dest = dest;
		md->next_ptr->remove = 0;
		md->next_ptr->weight = 1;
		*move_counter += 1;
		md->next_ptr ++;
	}

	gm->board[dest] = 0;
	if(source)
		gm->board[source] = prev_source;
}

static int gen_movements(struct model_data *md, struct state_key state_key)
{
	uint8_t board[25];
	memset(board, 0, 25);
	// deserialize state_key
	{
		md->p1_count = md->p2_count = 0;
		uint64_t _board = state_key.board;
		for(int i=0;i<24;i++)
		{
			uint8_t piece = _board & 3;
			board[i+1] = piece;
			switch(piece)
			{
			case 1:
				md->p1_places[md->p1_count++] = i+1;
				break;
			case 2:
				md->p2_places[md->p2_count++] = i+1;
				break;
			default:;
			}
			_board >>= 2;
		}
	}

	// and now, generate moves
	{
		int to_move, other, my_piece_count, other_piece_count;
		uint8_t *my_places, *other_places;
		struct gm_data gmd;
		//printf("to_move = %d\n", state_key.to_move);
		switch(state_key.to_move)
		{
		case 1:
			my_places = md->p1_places;
			other_places = md->p2_places;
			my_piece_count = md->p1_count;
			other_piece_count = md->p2_count;
			break;
		case 2:
			my_places = md->p2_places;
			other_places = md->p1_places;
			my_piece_count = md->p2_count;
			other_piece_count = md->p1_count;
			break;
		default: ;
		}
		to_move = state_key.to_move;
		other = to_move ^ 3;
		gmd = (struct gm_data){
			to_move,
			other,
			my_piece_count,
			other_piece_count,
			my_places,
			other_places,
			board
		};

		md->state->state_key = state_key;
		md->state->place_moves = 0;
		md->state->slide_moves = 0;
		md->next_ptr = (struct move*)(md->buffer+sizeof(struct state));

		// place moves
		for(int place=1;place<=24;place++)
		{
			if(board[place])
				continue;
			_do_move(md, &md->state->place_moves, &gmd, 0, place);
		}
		if(my_piece_count < 3 || other_piece_count < 3)
			goto _no_slide;
		// slide moves
		for(int i=0;i<my_piece_count;i++)
		{
			int source = my_places[i];
			if(my_piece_count == 3)
			{
				// move anywhere
				for(int dest = 1; dest<=24; dest++)
				{
					if(board[dest])
						continue;
					_do_move(md, &md->state->slide_moves, &gmd, source, dest);
				}
			}
			else
			{
				// just neighbours
				for(uint8_t *nei = board_moves[source]; *nei; nei++)
				{
					if(board[*nei])
						continue;
					_do_move(md, &md->state->slide_moves, &gmd, source, *nei);
				}
			}
		}
		// assert(slide_moves > 0)
		// without creating a backtrace into assert or anything
		board[0] = 1/md->state->slide_moves;

	_no_slide:
		// insert into database
		unqlite_int64 val_size = (void*)md->next_ptr - md->buffer;
		if(unqlite_kv_store(md->db, &state_key, sizeof(state_key), md->buffer, val_size) != UNQLITE_OK)
		{
			fprintf(stderr, "failed to store in database\n");
			return 1;
		}
		if(unqlite_commit(md->db) != UNQLITE_OK)
		{
			fprintf(stderr, "failed to commit transaction\n");
			return 1;
		}
	}

	// else
	return 0;
}
