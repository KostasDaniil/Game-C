#include <stdio.h>
#include <stdlib.h>
#include "maze.h"

// useful Variables
int grid;       // total size of arrays
int SIZE;       // user specified size
int cells;      // total visitable cells
char *m;        //maze array
int b[4] = {0,0,0,0};   //gives the block status while carving maze
int visitCount = 0; //to track visited cells while carving

// for filling array with wall characters ASCII 219
void initializeM(int n){
	int i, j;
	grid = 2*n+1;
	cells = n * n;
	m = (char *) malloc(grid*grid*sizeof(char));
	for(i = 0; i < grid; ++i){
		for(j = 0; j < grid; ++j){
			*(m + (i*grid) + j) = '|';
		}
	}

	*(m + (0*grid) + 1) =  ' ';
	*(m + (2*n*grid) + grid-2) = ' ';
}

void displayM(){
	int i, j;
	for(i = 0; i<grid; ++i){
		for(j = 0; j<grid; ++j){
			printf("%c%c",*(m + (i*grid) + j),*(m + (i*grid) + j));
		}
		printf("\n");
	}
	printf("\n");
}

char* get_maze(){
	return m;
}

// To set the visited status of neighbours of a given cell, 0 for available, 1 for not available
void setBlocked(int x, int y){
	int i;
	for(i = 0; i < 4; ++i) b[i] = 0;
	if(y == 1 || *(m + ((y-2)*grid) + x) == ' ') b[0] = 1;
	if(y == grid - 2 || *(m + ((y+2)*grid) + x) == ' ') b[1] = 1;
	if(x == 1 || *(m + (y*grid) + x - 2) == ' ') b[2] = 1;
	if(x == grid - 2 || *(m + (y*grid) + x + 2) == ' ') b[3] = 1;
}

// To get a random direction towards non visited neighbours, 0 for up, 1 for down, 2 for left, 3 for right
int getDirection(int x, int y){

	setBlocked(x,y);
	int r;
	r = rand() / (RAND_MAX / 4);
	while(1){
		if(b[r] == 0){
			break;
		} else {
			r = rand() % 4;
		}
	}
	return r;
}

// To check for dead ends at a cell position
int deadEnd(int x, int y){
	int i,flag = 0;
	setBlocked(x,y);
	for(i = 0; i < 4; ++i){
		if(b[i] == 0){
			flag = 0;
			break;
		} else {
			flag = 1;
		}
	}
	return flag;
}

// Main carving function, calls itself recursively based on the direction from getDirection()
void carvePath(int x, int y){
	if(*(m + (y*grid) + x) != ' '){
		++visitCount;
	}
	*(m + (y*grid) + x) = ' ';
	if(visitCount != cells){
		while(deadEnd(x,y) == 0){
			int dir = getDirection(x,y);
			switch(dir) {
				case 0: *(m + ((y-1)*grid) + x) = ' '; carvePath(x,y - 2); break; //up case
				case 1: *(m + ((y+1)*grid) + x) = ' '; carvePath(x,y + 2); break; //down
				case 2: *(m + (y*grid) + x - 1) = ' '; carvePath(x - 2,y); break; //left
				case 3: *(m + (y*grid) + x + 1) = ' '; carvePath(x + 2,y); break; //right
			}
		}
	}
}
