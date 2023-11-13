
MODEL_A ?= a.db
MODEL_B ?= b.db

default: learn

learn: learn.o model.o
	$(CC) -o learn learn.o model.o -l unqlite -ggdb

learn.o: learn.c structs.h model.h
	$(CC) -c learn.c -ggdb

model.o: model.c structs.h model.h
	$(CC) -c model.c -ggdb

clean:
	rm -f *.o

run: learn
	./learn models/$(MODEL_A) models/$(MODEL_B)
