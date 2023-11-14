# 9 Men's Morris ML

_A machine learning implementation of 9 men's morris_

This is done as a learning exercise for machine learning (I'm not a student
tho) and to have an opponent to play with :)

Since this is a fairly simple game (but more complex than tic-tac-toe),
I thought it would be a good candidate to try, for the first time, to
implement a machine learning model.

For each state (of the board/game), it picks randomly (weighted) from all
the possible moves and keeps a list of all the moves done.

If the model wins, it will increment the weight of the moves it picked.
(I guess that's the "reinforced" part of the learning)

For storage, it uses `unqlite`, with the state of the board (piece placement
and who-to-move) as key, with the value being the (variadic) list of weighted
possible moves.

To run the learning, simply _pit_ two models against each other:

```
$ ./learn models/a.db models/b.db
```

> I usually keep 4 models and pit every one at the other in a round robin
> fashion.

> TODO: implement a way to play against the model
