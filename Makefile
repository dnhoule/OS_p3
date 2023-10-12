CC = gcc
CFLAGS = -g -Wall -Werror -pedantic -std=gnu18
LOGIN = houle
SUBMITPATH = ~cs537-1/handin/houle/P3


all: wsh
.PHONY: all f

wsh: wsh.c wsh.h
	$(CC) $(CFLAGS) $< -o $@

run: wsh
	./$<

pack: wsh.c wsh.h Makefile README.md
	tar -zcvf $(LOGIN).tar.gz $^

submit: pack
	cp $(LOGIN).tar.gz $(SUBMITPATH)