# 9 man's morris

2 phases of the game:
- placement: players are still to place pieces
- sliding: players may slide pieces (when there are only 3 pieces of a color, players may move freely)

when a player has only 2 pieces, the game ends

a player may remove a opponent's piece if they are able to make a line of 3;
a player may not remove a piece that is on a 3 line;

A simple state-based approach is to be taken

## Drawing states

There are (3 to) 9 pieces for 2 players distributed in 24 places (A to Y, 1 to 24)
(in placement phase, 0 to 9 pieces may exist)

depending on the placement, there is a variant number of possible moves, for each player
to go, that is:

- for each of the (up to) 9 pieces, it can slide at most in 4 directions, giving at most 36 possible moves;
- for each of the 3 pieces (special case), it can move to up to 19 places (at least 6 places of the 24 are taken)
  giving at most 57 moves.

So a maximum of 57 moves are possible, for each state.
Moves may also remove an opponents piece, which can be in up to 9 places,
so that brings the tally to 66 moves.

This can safely be stored in 8 bits.

Player black is to move first

A ternary state for each place is taken (0 empty, 1 player 1, 2 player 2)
> that's 2 bits each place, 48 bits states (will be stored in 64 bits)

So a state can be a single 64 bit integer, with the 48 bits to the state, and the
rest is ignored,

A move shall be origin (if any) to destination, since 24 fit in 5 bits (32),
As a move may remove a piece, a further 24 possible values are possible, that
brings us to 15 bits (3 * 5), which we can store in a 16 bit integer, using bitfields

For machine learning on a reinforced way, the machine, for the state it is in,
it needs to draw a random move, taken the weights of the move.

(The winning player will reward the chain of moves)

Here are the C structs
```
struct move {
	uint16_t source : 5;
	uint16_t dest   : 5;
	uint16_t remove : 6;	// add the padding
	uint16_t weight;
};

struct state_key {
	uint64_t board  : 48;
	uint8_t _pad    : 8;
	uint8_t to_move : 8;
};

struct state {
	struct state_key state_key;
	uint8_t place_moves;
	uint8_t slide_moves;
};
```

### Storing states

To store the state, a BTree/B+Tree can be used:

The key shall be the board+to_move struct, and the value shall be
the `state` structure (including key), followed by `place_moves` amount
of `move`s followed by `slide_moves` amount of `move`s:

```
+----- 16 -----+----- 8 -----+-----+----- 8 -----+-----+
|    state     | place_move  | ... | slide_move  | ... |
+--------------+-------------+-----+-------------+-----+
```

### Generating states

Generation/enumeration requires 2/3 steps:

1. Enumerate number of pieces for player;
2. Enumerate the combinations of how to place those pieces on the board.

The second, because we're placing 2 colors of pieces, can be split into 2 phases:

2. Place the first players pieces, a simple binomial coefficient to place N pieces on the 24 places board;
3. Place the second player pieces, another binomial coefficient to place the M pieces on the remaining places.

To generate the number of pieces, since each player may have 0 to 9 pieces (that's decimal)
we can simply count from 00 to 99. To note that not every combination is possible/feasible,
but we will still keep them for easeness.

For the placement of the players' pieces, we will generate the possibilites using
lexicographic order. Namely a 24 bit integer (32 but the rest is ignored) counter
that only has N bits set. We can use the `popcnt` instruction if the CPU supports it,
otherwise, Knut's "Algorithm L" shall be used.

### Generating moves

A board with all the possible moves for each position shall be provided,
to generate the moves we'll just iterate everything.

## Playing and Learning

To play and learn, two models will be waged against each other, during multiple
games with the first to play being changed every game.

The model will be instantiated with a reference to a LMDB's database environment.

Each model will have an instantiation of a structure, in that structure, it will
keep track of the moves it has made during the game. If a win was achieved,
it will reinforce the moves it has made.

> The moves are made randomly accorrding to the current `weight`.
> By winning, the weight of the moves made will increase

> In terms of LMDB storage, each model, for each game, can open
> a write transaction and update the weights as it goes.
> If it loses, the transaction is dropped :)

For each round, the board state will be provided to the model to play and
the model is expected to return a (valid) move. Alas the board will get updated.

### Generating states on the fly

Instead of having a program generating all possible states beforehand,
the model, when presented with a state that is unknown, can generate,
on the fly, the initial values (weight) of the state, from  all
possible moves and store it already on the database.
