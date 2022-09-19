#!/bin/sh
g++ -Os ./src/main.cpp -o tinyhttp
x86_64-w64-mingw32-g++ -Os -std=c++20 ./src/main.cpp -o tinyhttp.exe -lws2_32 -static-libstdc++ -static-libgcc

