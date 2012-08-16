#!/bin/sh

gcc -g -O0 -DPYZOR_DEBUG -o pyzor pyzor.c main.c `pkg-config --cflags --libs gmime-2.6`
