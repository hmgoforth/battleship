/**
 * Battleship
 *
 * Authors: Hunter Goforth, Erin Long, David Suh
 * Date: 3/6/16
 *
 * battleship.c is an implementation of the two-player game Battleship. In Battleship, each player
 * assigns locations of ships to indices on a grid. Then the players attempt to guess where each
 * other placed their ships. The player to guess the location of all of the other's ships is
 * the winner.
 *
 * This version of Battleship is played on the console, and the players communicate through the
 * an Altera FPGA board. The board uses a custom designed SRAM module to store the states of
 * each players' board as the game progresses.
 */

#include "sys/alt_stdio.h"
#include "altera_avalon_pio_regs.h"
#include <unistd.h>

// SRAM (Processor Inputs and Outputs)
#define data (char*) 0x21000
#define address (char*) 0x21010
#define chipSelect (char*) 0x21020
#define readnWrite (char*) 0x21030
#define notOutEn (char*) 0x21040

// Communications System (Processor Inputs and Outputs)
#define char_read (char*) 0x21050
#define char_recv (char*) 0x21060
#define data_in (char*) 0x21070
#define load (char*) 0x21080
#define char_sent (char*) 0x21090
#define trans_en (char*) 0x210a0
#define data_out (char*) 0x210b0
#define LEDs (char*) 0x210c0

// Global Constants
#define bufLen 10
#define SMALL_SHIP_LENGTH 3
#define LARGE_SHIP_LENGTH 4
#define BOARD_WIDTH (unsigned int) 8
#define BOARD_HEIGHT (unsigned int) 8
#define shotsBase (unsigned int) 0
#define hitsBase (unsigned int) 8
#define boardBase (unsigned int) 16

// Global Variables
unsigned char inputBuffer[bufLen];
unsigned char outputBuffer[bufLen];
unsigned char sent;
unsigned char parity;
unsigned int enemyHits;
unsigned int yourHits = 0;
unsigned int yourShotX;
unsigned int yourShotY;
unsigned int theirShotX;
unsigned int theirShotY;

/**
 * charToInt() takes a character and returns the
 * corresponding integer, based on the Battleship
 * game rules. ('A' and '1' return 1, 'B' and '2'
 * return 2, etc)
 */
unsigned int charToInt(unsigned char c) {
    return c > '@' ? c - '@' : c - '0';
}

/**
 * enterString() allows the user to fill the
 * output buffer with a string of characters
 * ending with a null terminator
 */
void enterString() {
	int i = 0;
	unsigned char c = alt_getchar();
	while (c != '\n') {
		outputBuffer[i] = c;
		c = alt_getchar();
		i++;
	}
	outputBuffer[i] = '\0';
}

/**
 * checkIndex() returns 0 if the given
 * index is within the bounds of the game
 * board, and 1 otherwise
 */
int checkIndex(int x, int y) {
    if (x >= 0 && x <= BOARD_WIDTH &&
        y >= 0 && y <= BOARD_HEIGHT) {
        return 0;
    }
    return 1;
}

/**
 * computeParity takes a character,
 * calculates its parity, and returns
 * it as an unsigned char
 */
unsigned char computeParity(char character) {
	character ^= character >> 4;
	character ^= character >> 2;
	character ^= character >> 1;
	character &= 1;
	return character;
}

/**
 * createByte is a utility function that takes
 * an index and returns an 8 bit binary (char)
 * that is zero everywhere besides that index
 */
unsigned char createByte(int x) {
	unsigned char c = 1;
	return c << (BOARD_WIDTH - x);
}

/**
 * sendChar sends a single character across
 * the data link between the two FPGA boards
 */
void sendChar(char c) {
	alt_printf("currently sending ");
	alt_putchar(c);
	alt_putchar('\n');
	sent = c;
	parity = sent;
	parity = computeParity(parity);
	sent <<= 1;
	sent = parity + sent;
	*data_out = sent;
	usleep(5);
	*load = 1;
	usleep(5);
	*trans_en = 1;
	usleep(5);
	*load = 0;
	while (*char_sent == 0) {
		alt_printf("");
	}
	*trans_en = 0;
	usleep(100);
}

/**
 * readString() reads a null-terminated
 * string of characters being sent from
 * the other player's FPGA board and
 * stores it to the input buffer
 */
int readString() {
	int i = 0;
	unsigned char received = 1;
	while (received != '\0') {
		if (*char_recv) {
			received = *data_in;
			parity = received & 1;
			received >>= 1;
			if (computeParity(received) == parity) {
				inputBuffer[i] = received;
				*char_read = 1;
				usleep(5);
				*char_read = 0;
			} else {
				alt_printf("Error: Received byte \"%c\" which has incorrect parity bit\n", *data_in);
				*char_read = 1;
				usleep(5);
				*char_read = 0;
				return 1;
			}
			i++;
		}
	}
	return 0;
}

/**
 * sendString() sequentially sends each
 * of the characters in the null-terminated
 * string that is contained within the
 * output buffer
 */
void sendString() {
	int i;
	for (i = 0; i < bufLen; i++) {
		if (outputBuffer[i] == '\0') {
			sendChar(outputBuffer[i]);
			break;
		} else {
			sendChar(outputBuffer[i]);
		}
	}
}

/**
 * prinInput() prints the null-terminated
 * string contained within the input buffer
 */
void printInput() {
	int i = 0;
	while (inputBuffer[i] != '\0') {
		alt_putchar(inputBuffer[i]);
		i++;
	}
	alt_putchar('\n');
}

/**
 * readSRAM() returns the byte inside
 * the SRAM pointed to by the given integer
 * address (between 0 and 2047)
 */
unsigned char readSRAM(int addr) {
	unsigned char out;
	*address = addr;
	*notOutEn = 0;
	out = *data;
	usleep(10);
	*notOutEn = 1;
	return out;
}

/**
 * writeSRAM() stores the given byte
 * at the given zero-based integer
 * address in SRAM (between 0 and 2047)
 */
void writeSRAM(int addr, unsigned char byte) {
	*address = addr;
	*data = byte;
	*readnWrite = 0;
	usleep(1);
	*readnWrite = 1;
}

/**
 * checkMove() checks the given board
 * to see if the given index is set high already
 * and returns 1 if it is set high.
 */
int checkMove (unsigned int x, unsigned int y, unsigned int boardAddr) {
    unsigned char byte;
	byte = readSRAM(boardAddr + (y - 1));
	byte = byte >> (BOARD_WIDTH - x);
	return byte & 1;
}

/**
 * translateOutputBuffer() reads the current
 * two-character index that is stored in the output
 * buffer, converts it from the traditional
 * Battleship index notation (e.g. 'A1' or 'C7') to
 * normal integers (handier when in the program for shifting
 * bytes or working with SRAM)
 */
void translateOutputBuffer() {
	yourShotY = charToInt(outputBuffer[0]);
	yourShotX = charToInt(outputBuffer[1]);
}

/**
 * translateInputBuffer() serves the same function as
 * translateOutputBuffer(), except for the input buffer
 */
void translateInputBuffer() {
	theirShotY = charToInt(inputBuffer[0]);
	theirShotX = charToInt(inputBuffer[1]);
}

/**
 * updateEnemyBoard() stores a new state of one
 * of the enemy game boards in SRAM. Which game board
 * (shots you have made on their board, or hits you
 * have made on their board) is determined by the base
 * address passed to the function. The update that
 * is made is based the value of the global variables
 * yourShotX and yourShotY; the index indicated by
 * those values will be set high in the board that is passed
 */
void updateEnemyBoard(int board) {
	unsigned char row;
	row = readSRAM(yourShotY - 1 + board);
	alt_printf("Current values at row %x: %x\n", yourShotY, row);
	unsigned char newRow;
	newRow = row | createByte(yourShotX);
	alt_printf("Inserting at row %x: %x\n", yourShotY, newRow);
	writeSRAM(yourShotY - 1 + board, newRow);
}

/**
 * updateYourBoard() uses the global variables theirShotX and
 * theirShotY to update your game board. The function determines
 * whether the enemy's shot was a hit or miss, indicates this on the
 * console, and updates your game board and boat count
 */
void updateYourBoard() {
	int hit;
    unsigned char byte = readSRAM(boardBase + theirShotY - 1);
    alt_printf("The row byte for row %x is %x \n", theirShotY, byte);
	hit = (byte >> (BOARD_WIDTH - theirShotX)) & 0x01;
	if (hit) {
		alt_printf("Enemy got a hit\n");
		writeSRAM(boardBase + theirShotY - 1, ~createByte(theirShotX) & byte);
		enemyHits++;
		outputBuffer[0] = '1';
		outputBuffer[1] = '\0';
	} else {
		alt_printf("Enemy has missed\n");
		outputBuffer[0] = '0';
		outputBuffer[1] = '\0';
	}
}

/**
 * printEnemyBoard() returns void
 * Prints an ASCII representation of the enemy's current board state
 * Confirmed hits are marked with "X", Missed shots are marked with "O",
 * empty space is marked with "-"
 */
void printEnemyBoard() {
	unsigned char byteShot;
	unsigned char byteHit;
	int ishift;
	int ishot = shotsBase;
	int ihit = hitsBase;
	int bitShot;
	int bitHit;
	alt_printf("Current assessment of enemy territory...\n");
	alt_printf("  1 2 3 4 5 6 7 8\n");
	while (ishot < shotsBase + 8) {
		alt_printf("%c ", 'A' + ishot - shotsBase);
		byteShot = readSRAM(ishot);
		byteHit = readSRAM(ihit);
		for (ishift = 7; ishift >= 0; ishift--) {
			bitShot = (byteShot >> ishift) & 0x01;
			bitHit = (byteHit >> ishift) & 0x01;
			if (bitShot & bitHit) {
				alt_putchar('X');
			} else if (bitShot) {
				alt_putchar('O');
			} else {
				alt_putchar('-');
			}
			alt_putchar(' ');
		}
		alt_printf("\n");
		ishot++;
		ihit++;
	}
	alt_printf("\n");
}

/**
 * printYourBoard() returns void
 * Prints an ASCII representation of a player's current board state
 * Boats are marked with "B", empty space is marked with "-"
 */
void printYourBoard() {
	unsigned char byteBoard;
	int ishift;
	int iboard = boardBase;
	int bitBoard;
	alt_printf("Your fleet...\n");
	alt_printf("  1 2 3 4 5 6 7 8\n");
	while (iboard < boardBase + 8) {
		alt_printf("%c ", 'A' + iboard - boardBase);
		byteBoard = readSRAM(iboard);
		for (ishift = 7; ishift >= 0; ishift--) {
			bitBoard = (byteBoard >> ishift) & 0x01;
			if (bitBoard) {
				alt_putchar('B');
			} else {
				alt_putchar('-');
			}
			alt_putchar(' ');
		}
		alt_printf("\n");
		iboard++;
	}
	alt_printf("\n");
}

/**
 * eraseSRAM() returns void
 * Routine to clear all data on the SRAM
 */
void eraseSRAM() {
	int i;
	for (i = 0; i < 30; i++) {
		writeSRAM(i, 0);
	}
}

/**
 * setIndexHigh() return void
 * Accepts integers x and y for coordinates, and a base address for a game board
 * Uses the coordinates to "turn on" a bit at that location on the given board
 */
void setIndexHigh (int x, int y, int base) {
	unsigned char byte;
	byte = readSRAM(base + y - 1);
	byte = createByte(x) | byte;
	writeSRAM(base + y - 1, byte);
}



/**
 * setUpBoats() returns void
 * Initializes game by placing the player's boats on their board
 * sizes of the boats are bound by LARGE_SHIP_LENGTH and SMALL_SHIP_LENGTH
 * where each subsequent boat will be 1 unit smaller than before
 * The user will be prompted for a coordinate and an orientation
 * 'v' assumes the ship is placed at the given coordinate and continued down
 * 'h' assumes the ship is placed at the given coordinate and continued right
 */
void setUpBoats() {
    int i;
	int j;
	int check = 0;
	unsigned char xCoor;
	unsigned char yCoor;
	unsigned char orientation;
	printYourBoard();
	for (i = LARGE_SHIP_LENGTH; i >= SMALL_SHIP_LENGTH; i--) {
		do {
			alt_printf("Please choose coordinates for your length %x ship: ", i);
			yCoor = charToInt(alt_getchar());
			xCoor = charToInt(alt_getchar());
			alt_getchar();
			do {
				alt_printf("Please choose either vertical or horizontal orientation (v or h): ");
				orientation = alt_getchar();
				alt_getchar();
			} while (orientation != 'h' && orientation != 'v');

			for (j = i - 1; j >= 0; j--) {
				if (orientation == 'v') {
					check = checkMove(xCoor, yCoor + j, boardBase) ||
							checkIndex(xCoor, yCoor + j);
				} else {
					check = checkMove(xCoor + j, yCoor, boardBase) ||
							checkIndex(xCoor + j, yCoor);
				}
				if (check) {
					alt_printf("Sorry, that location is off the map or already taken\n");
					break;
				}
			}

			if (!check) {
				for (j = i - 1; j >= 0; j--) {
					if (orientation == 'v') {
						setIndexHigh(xCoor, yCoor + j, boardBase);
					} else {
						setIndexHigh(xCoor + j, yCoor, boardBase);
					}
				}
			}
		} while (check);
		printYourBoard();
	}
}

int main() {
	alt_printf("+ooooooo++:`      /oooooooo.  .ooooooooooooo oooooooooooo+ /ooooo-    -ooooooooo+   .+shhhhyo:    /ooooo  /ooooo- `oooooo  :ooooooo++:`\n");
	alt_printf("dMMMMMMMMMMMh.    mMMMMMMMMs  :MMMMMMMMMMMMM MMMMMMMMMMMMm dMMMMM/    +MMMMMMMMMd  yMMMMMMMMMMN:  yMMMMM  hMMMMM+ .MMMMMM  sMMMMMMMMMMMh`\n");
	alt_printf("dMMMMMysNMMMMd   .MMMMMMMMMm  -mmmNMMMMMNmmm mmmMMMMMMmmmh dMMMMM/    +MMMMMNmmmy +MMMMM  NMMMMd  yMMMMM  hMMMMM+ .MMMMMM  sMMMMMdsmMMMMo\n");
    alt_printf("dMMMMM   MMMMM   +MMMMNmMMMM.     yMMMMMo       NMMMMM-    dMMMMM/    +MMMMMh     sMMMMM  dMMMMN  yMMMMM  hMMMMM+ .MMMMMM  sMMMMM   MMMMy\n");
    alt_printf("dMMMMM   MMMMN   yMMMMhhMMMM+     yMMMMMo       NMMMMM-    dMMMMM/    +MMMMMh     /MMMMMNo        yMMMMM  dMMMMM+ .MMMMMM  sMMMMM   MMMMy\n");
    alt_printf("dMMMMMNNMMMms-   NMMMM  MMMMh     yMMMMMo       NMMMMM-    dMMMMM/    +MMMMMMNNNo  sMMMMMMMms-    yMMMMMMMMMMMMM+ .MMMMMM  sMMMMM  NMMMMs\n");
    alt_printf("dMMMMMNMMMMMm+  -MMMMM  MMMMM`    yMMMMMo       NMMMMM-    dMMMMM/    +MMMMMMMMMo   .omMMMMMMMd-  yMMMMMMMMMMMMM+ .MMMMMM  sMMMMMMMMMMMm.\n");
    alt_printf("dMMMMM   MMMMM: oMMMMM  MMMMM/    yMMMMMo       NMMMMM-    dMMMMM/    +MMMMMd:::.      -sNMMMMMN` yMMMMM  dMMMMM+ .MMMMMM  sMMMMMdoo+/-\n");
    alt_printf("dMMMMM   MMMMM+ hMMMMMMMMMMMMy    yMMMMMo       NMMMMM-    dMMMMM/    +MMMMMh     /MMMMM:  MMMMM/ yMMMMM  hMMMMM+ .MMMMMM  sMMMMMs\n");
    alt_printf("dMMMMM   MMMMM+`MMMMMMMMMMMMMN    yMMMMMo       NMMMMM-    dMMMMM+... +MMMMMh.... :MMMMM/  MMMMM+ yMMMMM  hMMMMM+ .MMMMMM  sMMMMMs\n");
    alt_printf("dMMMMMmNMMMMMM::MMMMMN  dMMMMM-   yMMMMMo       NMMMMM-    dMMMMMMMMM`+MMMMMMMMMM-`NMMMMdsmMMMMM- yMMMMM  hMMMMM+ .MMMMMM  sMMMMMs\n");
    alt_printf("dMMMMMMMMMMMNo sMMMMMh  yMMMMMs   yMMMMMo       NMMMMM-    dMMMMMMMMM`+MMMMMMMMMM- .yNMMMMMMMMd:  yMMMMM  hMMMMM+ .MMMMMM  sMMMMMs\n");
    alt_printf(".--------..`   .-----.  `-----.   .-----`       ------`    .--------- `----------     -/+++/-`    .-----` .-----`  ------  `-----.\n");
    alt_printf("                                                                           ```-y:`\n");
    alt_printf("                                                                           ../smmo- \n");
    alt_printf("                                                                           -  .mh\n");
    alt_printf("                                                                        `mNm``dh\n");
	alt_printf("                                                                       :/yNo..dh\n");
	alt_printf("                                                                       dmNNNNmNh\n");
	alt_printf("                                                                 `+o:`   -dd+/s.\n");
	alt_printf("                                                                 hNNNN-  :dhy.\n");
	alt_printf("                                                            ...-://dNh-.`-dys\n");
    alt_printf("                                                        .:/:..-ydNNNNNNNh:dyy\n");
	alt_printf("                                                      -dNNNNNh.`:NNNNNNNm:dyd/y:\n");
	alt_printf("                                                      yNNNNNNNy .NNNNNNNm:NNmyNo\n");
	alt_printf("                                                     hNNNNNNNh-+NNNNNNNNNNNdoo:\n");
	alt_printf("  ``:                                             ... dNNNNNNNNNNNNNNNNNNNNNN//-\n");
	alt_printf("sNNNy                                            .NNNmNNNNNNNNNNNNNNNNNNNNNNNyys++`\n");
	alt_printf("/Nms+o                           ``````           yNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNmhyo+:-`         :oo-.`+ys:.`                    `\n");
	alt_printf(":-   :                   ``  ``:hNNNNNN+          NNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNdooooo/`-mNNm/+NNNm/.                   ++\n");
	alt_printf("     :                   ..--/hNNNNNNNNy         .NNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNmhhhmhyyy+    `.   oN/\n");
	alt_printf("    :/.-.:..:.-..:..-..-..-..-ydNNNNNNNo-++/-```-+NNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNN.  -mNo `hh-\n");
	alt_printf("    `omNNNmmmmmdddhhhyyhssyoossmNNNNNNNhdNNNNyooNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNy-+NNNN+/+++:-..\n");
	alt_printf("      `sNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNo\n");
	alt_printf("        .sNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNo\n");
	alt_printf("          .hNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNN:\n");
	alt_printf("            /NNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNm`\n");
	alt_printf("             .hNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNmmmmmmmmmmmmmmmmmmmdddddddddddddddddhhhhhhhhhs\n");
	alt_printf("              :hdddhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyys:\n");
	alt_printf("            .oyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyysssssssssssssssssssssssssssssssssssssssssssssso\n");
	alt_printf("          `/yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssso+oooooo++++//::-\n");
	alt_printf("        `:ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssossoooooooooooooooooooooooooooooooooooooooooo+  .+oo. ./.\n");
	alt_printf("      `/osssssssssssssooooo+++/oosssoooo/ooooo:-+oooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo++++++++++.   `-`  `+/\n");
	alt_printf("     .//:::-:--:.-..-``.``.```./+oooooo+....`     /oooooooooooooooooo+++++++++++++++++++++++++++++++/:/++++:+++++:`` ``              `:`\n");
	alt_printf("     `-```````            ```.-/++++++++.         -+++++++++++++++++++++++++++++++++++++++//:-.``     `:/:.``:::``\n");
	alt_printf(" `    .                         `-:::---          `/++++++++++++////////////////////--..`\n");
	alt_printf(" :/:-.-                                            .///:///////////////////////`-\n");
	alt_printf(" `-::/-                                                 ://////:::::::::::::::-`.`\n");
	alt_printf("                                                        -:::::::. `::::::::.::--:.\n");
	alt_printf("                                                        .-:::::-` `--------`-.-`.`\n");
	alt_printf("                                                          ``.`` `..-------.`...\n");
	alt_printf("                                                                   ``...`  `...\n");
	alt_printf("                                                                   `...`    `````\n");
	alt_printf("                                                                          ````````\n");


	alt_printf("Welcome to the warzone!\n");
	alt_printf("The first rule of Battleship is that the last man standing wins. Aside from that, here are some guidelines:\n");
	alt_printf("\t- You will place your ships, starting from your biggest ship (length %x) down to your smallest ship (length %x)\n");
	alt_printf("\t- The game uses coordinates like A1 and C6, where A - H are valid horizontal coordinates and 1 - 8 are valid vertical coordinates\n");
	alt_printf("\t- The commanders of the ships must agree upon the order in which the firefight shall commence (Player 1 and Player 2)\n");
	alt_printf("\t- Once the game is underway, each side may fire upon the other as his or her turn comes by entering a coordinate to fire upon\n");
	alt_printf("\t- Your map of the enemy territory shows O's where you have shot previously, and X's where you have shot and made a hit\n");
	alt_printf("\t- Your ships are displayed using B's to denote where you still have ships (or fragments of ships, at least)\n");
	alt_printf("\t- Artillery and shrapnel will follow, until such a point when either you or your enemy has succumbed to the cold blue depths of the Pacific\n");
	alt_printf("\t- The war is over, and the victorious side may now loot and plunder the land of the loser\n\n");
	alt_printf("Let the games begin!\n\n");

	*char_read = 1;
	usleep(5);
	*char_read = 0;
	eraseSRAM();

	int i;
	int totalHits = 0;
	for (i = SMALL_SHIP_LENGTH; i <= LARGE_SHIP_LENGTH; i++) {
		totalHits += i;
	}

	setUpBoats();

	int yourTurn;
	int player;
	int otherPlayer;
	int notValidMove = 1;
	alt_printf("Are you player 1 or 2? ");
	player = charToInt(alt_getchar());
	otherPlayer = 3 - player;
	yourTurn = 2 - player;
	alt_getchar();
	while (yourHits != totalHits && enemyHits != totalHits) {
		if (yourTurn) {
			while (notValidMove) {
				alt_printf("Please enter a coordinate to fire at: ");
				enterString();
				translateOutputBuffer();
				notValidMove = checkMove(yourShotX, yourShotY, shotsBase) ||
						checkIndex(yourShotX, yourShotY);
			}
			sendString();
			readString();
			alt_printf("Updating shots board:\n");
			updateEnemyBoard(shotsBase);
			translateInputBuffer();
			if (charToInt(inputBuffer[0])) {
				alt_printf("Updating hits board:\n");
				updateEnemyBoard(hitsBase);
				yourHits++;
			}
			printEnemyBoard();
			printYourBoard();
			notValidMove = 1;
			yourTurn = 0;
		} else {
			alt_printf("Waiting for player %x to make a move...", otherPlayer);
			readString();
			translateInputBuffer();
			alt_printf("Enemy has fired on coordinate %c%c\n", inputBuffer[0], inputBuffer[1]);
			alt_printf("Translates to integer coordinate %x%x\n", theirShotY, theirShotX);
			updateYourBoard();
			sendString();
			printEnemyBoard();
			printYourBoard();
			yourTurn = 1;
		}
	}

	if (yourHits == totalHits) {
		alt_printf("You sunk all of player %x ships! Game over...", otherPlayer);
	} else {
		alt_printf("The enemy has sunken all of your ships! Game over...");
	}

	return 0;
}
