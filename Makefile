# Makefile for Firebee

all:
	gcc firebee.c -o firebee -g -levent -lhiredis
