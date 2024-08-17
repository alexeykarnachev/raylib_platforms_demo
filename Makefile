all:
	gcc \
	-Wall \
	-o ./platforms \
	./src/*.c \
	-I ./include/ -L ./lib/ \
	-lraylib -lpthread -lm -ldl
