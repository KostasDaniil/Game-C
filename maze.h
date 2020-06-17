#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// for filling array with wall characters ASCII
void initializeM(int n);

void displayM();

char* get_maze();

// To set the visited status of neighbours of a given cell, 0 for available, 1 for not available
void setBlocked(int x, int y);

// To check for dead ends at a cell position
int deadEnd(int x, int y);

// To get a random direction towards non visited neighbours, 0 for up, 1 for down, 2 for left, 3 for right
int getDirection(int x, int y);

// Main carving function, calls itself recursively based on the direction from getDirection()
void carvePath(int x, int y);
