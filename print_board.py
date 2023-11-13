from sys import argv

board = bin(int(argv[1]))[2:]
if len(board) != 48:
    diff = 48 - len(board)
    board = ('0'*diff) + board

key = '+12'

places = [
    key[int(board[i:i+2], 2)]
    for i in range(0, 48, 2)
]

#print(places)

board_fmt = (
    "%c---------%c---------%c\n"
    "|         |         |\n"
    "|  %c------%c------%c  |\n"
    "|  |      |      |  |\n"
    "|  |   %c--%c--%c   |  |\n"
    "|  |   |     |   |  |\n"
    "%c--%c---%c     %c---%c--%c\n"
    "|  |   |     |   |  |\n"
    "|  |   %c--%c--%c   |  |\n"
    "|  |      |      |  |\n"
    "|  %c------%c------%c  |\n"
    "|         |         |\n"
    "%c---------%c---------%c\n"
)

print(board_fmt % tuple(places))
