#include <mega32.h>
#include <delay.h>

/*
============================================================
 ATmega32 + 8x8 DOT MATRIX - 6 MODE PROJECT
============================================================

ASSUMED HARDWARE
----------------
8x8 single-color LED matrix

PORTA -> ROWS
PORTC -> COLUMNS

PD0 -> KEY1
PD1 -> KEY2
PD2 -> KEY3

Keys are active-low and use ATmega32 internal pull-ups.

MATRIX POLARITY USED HERE
-------------------------
Selected row  = HIGH
Selected column = LOW
Pixel ON = row HIGH + column LOW

If your Proteus matrix has the opposite polarity, tell me
and we will invert the two matrix output functions.

MODE SELECTION
--------------
Power-up              -> MODE 1
KEY1 >= 1s + release  -> MODE 2
KEY2 >= 1s + release  -> MODE 3
KEY3 >= 1s + release  -> MODE 4
KEY1+KEY2 >= 1s + release -> MODE 5
KEY1+KEY3 >= 1s + release -> MODE 6

IMPORTANT
---------
The matrix is multiplexed. Only one row is electrically
selected at a time, but rows are refreshed fast enough that
the eye sees a continuous picture.

The animation timing is non-blocking:
- matrix refresh: about every 1 ms
- animation step: every 500 ms or 1000 ms
- key hold: must be >= 1 second
- command is accepted only after release
============================================================
*/

#define KEY1 0
#define KEY2 1
#define KEY3 2

#define ROW_PORT PORTA
#define COL_PORT PORTC

#define KEY_SAMPLE_MS 10
#define HOLD_TIME_MS 1000

/* 5x7 English digit font.
   Bit 4 = leftmost pixel, bit 0 = rightmost pixel. */
flash unsigned char digitFont[10][7] =
{
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, /* 2 */
    {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}, /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
    {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}, /* 5 */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}  /* 9 */
};

/* screen[row] contains the 8 column bits for that row. */
unsigned char screen[8];

/* Current animation state */
unsigned char mode = 1;
unsigned char animationIndex = 0;
unsigned char digit = 0;
unsigned int animationTime = 0;

/* Key state */
unsigned char previousKeys = 0;
unsigned char currentKeys = 0;
unsigned int holdTime = 0;
unsigned char waitingForRelease = 0;

/* ---------------------------------------------------------
   MATRIX
   --------------------------------------------------------- */

void matrix_all_off(void)
{
    /* No row selected, all columns inactive. */
    ROW_PORT = 0x00;
    COL_PORT = 0xFF;
}

void matrix_refresh_1ms(void)
{
    static unsigned char row = 0;
    unsigned char pattern;

    /* Blank first: reduces ghosting during row change. */
    matrix_all_off();

    /* Select one row. */
    ROW_PORT = (1 << row);

    pattern = screen[row];

    /* Common matrix arrangement used here:
       bit=1 in screen means pixel ON.
       Column LOW means ON. */
    COL_PORT = ~pattern;

    delay_us(200);

    row++;

    if (row >= 8)
        row = 0;
}

/* ---------------------------------------------------------
   SCREEN HELPERS
   --------------------------------------------------------- */

void screen_clear(void)
{
    unsigned char i;

    for (i = 0; i < 8; i++)
        screen[i] = 0x00;
}

/* MODE 1
   One complete vertical column moves:
   right -> left -> right
   One position = 500 ms. */
void draw_mode1(unsigned char index)
{
    screen_clear();

    if (index < 8)
        screen[0] = 0; /* keep compiler happy; no special action */

    if (index < 8)
    {
        unsigned char c = 7 - index;

        for (index = 0; index < 8; index++)
            screen[index] = (1 << c);
    }
    else
    {
        unsigned char c = index - 8;

        for (index = 0; index < 8; index++)
            screen[index] = (1 << c);
    }
}

/* MODE 2
   One LED travels around the OUTER BORDER.
   Start = top-right corner.
   28 unique positions make one complete loop. */
void draw_mode2(unsigned char step)
{
    unsigned char r;
    unsigned char c;

    screen_clear();

    step = step % 28;

    if (step < 8)
    {
        /* top: right -> left */
        r = 0;
        c = 7 - step;
    }
    else if (step < 15)
    {
        /* left side: top -> bottom */
        r = step - 7;
        c = 0;
    }
    else if (step < 22)
    {
        /* bottom: left -> right */
        r = 7;
        c = step - 15;
    }
    else
    {
        /* right side: bottom -> top */
        r = 28 - step;
        c = 7;
    }

    screen[r] = (1 << c);
}

/* Draw a normal 5x7 digit in the 8x8 matrix.
   It is vertically centered.
   Horizontal placement:
       1 blank + 5 digit columns + 2 blank
*/
void draw_digit(unsigned char d)
{
    unsigned char r;

    screen_clear();

    for (r = 0; r < 7; r++)
        screen[r] = digitFont[d][r] << 2;
}

/* MODE 4 = negative of MODE 3 */
void draw_negative_digit(unsigned char d)
{
    unsigned char r;

    draw_digit(d);

    for (r = 0; r < 8; r++)
        screen[r] = ~screen[r];
}

/* MODE 5
   90-degree clockwise rotation of the 5x7 digit.
   The result is 7x5 and is placed in the 8x8 screen. */
void draw_rotated_digit(unsigned char d)
{
    unsigned char oldR;
    unsigned char oldC;
    unsigned char newR;
    unsigned char newC;
    unsigned char rowData;

    screen_clear();

    for (oldR = 0; oldR < 7; oldR++)
    {
        rowData = digitFont[d][oldR];

        for (oldC = 0; oldC < 5; oldC++)
        {
            if (rowData & (1 << (4 - oldC)))
            {
                /* 90 degrees clockwise */
                newR = oldC;
                newC = 6 - oldR;

                if (newR < 8 && newC < 8)
                    screen[newR] |= (1 << newC);
            }
        }
    }
}

/* MODE 6
   Scrolling digit:
   offset=8  -> completely outside right
   offset=7  -> first column enters
   ...
   offset=0  -> digit fully visible
   ...
   offset=-5 -> completely outside left

   One shift = 500 ms.
*/
void draw_scroll_digit(unsigned char d, signed char offset)
{
    unsigned char r;
    unsigned char c;
    unsigned char rowData;
    signed char newC;

    screen_clear();

    for (r = 0; r < 7; r++)
    {
        rowData = digitFont[d][r];

        for (c = 0; c < 5; c++)
        {
            if (rowData & (1 << (4 - c)))
            {
                newC = (signed char)c + offset;

                if ((newC >= 0) && (newC < 8))
                    screen[r] |= (1 << newC);
            }
        }
    }
}

/* ---------------------------------------------------------
   KEY HANDLING
   ---------------------------------------------------------

   Return:
      0 = no command
      1 = KEY1
      2 = KEY2
      3 = KEY3
      4 = KEY1+KEY2
      5 = KEY1+KEY3

   A key combination must remain pressed for >= 1 second.
   The mode changes only after the keys are released.
*/
unsigned char key_command_10ms(void)
{
    unsigned char keys;
    unsigned char k1;
    unsigned char k2;
    unsigned char k3;

    /* Active-low:
       pressed = 1 inside this variable */
    keys = (~PIND) & 0x07;

    k1 = keys & (1 << KEY1);
    k2 = keys & (1 << KEY2);
    k3 = keys & (1 << KEY3);

    /* No key currently pressed */
    if (keys == 0)
    {
        if (waitingForRelease)
        {
            waitingForRelease = 0;

            if (holdTime >= HOLD_TIME_MS)
            {
                /* previousKeys contains the accepted combination */
                if ((previousKeys & 0x07) == 0x03)
                    return 4; /* KEY1+KEY2 */

                if ((previousKeys & 0x07) == 0x05)
                    return 5; /* KEY1+KEY3 */

                if ((previousKeys & 0x07) == 0x01)
                    return 1; /* KEY1 */

                if ((previousKeys & 0x07) == 0x02)
                    return 2; /* KEY2 */

                if ((previousKeys & 0x07) == 0x04)
                    return 3; /* KEY3 */
            }

            holdTime = 0;
            previousKeys = 0;
        }

        return 0;
    }

    /* A key is pressed. Start/continue timing. */
    if (!waitingForRelease)
    {
        previousKeys = keys;
        holdTime = 0;
        waitingForRelease = 1;
    }

    /* Combination must stay unchanged while being held. */
    if (keys != previousKeys)
    {
        /* If another key is added/removed, restart the 1-second timer. */
        previousKeys = keys;
        holdTime = 0;
    }
    else
    {
        if (holdTime < HOLD_TIME_MS)
            holdTime += KEY_SAMPLE_MS;
    }

    return 0;
}

/* ---------------------------------------------------------
   APPLY COMMAND
   --------------------------------------------------------- */

void set_mode(unsigned char command)
{
    if (command == 1)
        mode = 2;
    else if (command == 2)
        mode = 3;
    else if (command == 3)
        mode = 4;
    else if (command == 4)
        mode = 5;
    else if (command == 5)
        mode = 6;

    animationIndex = 0;
    digit = 0;
    animationTime = 0;
    screen_clear();
}

/* ---------------------------------------------------------
   MAIN
   --------------------------------------------------------- */

void main(void)
{
    unsigned int keyTimer = 0;
    unsigned char command = 0;
    unsigned char refreshTimer = 0;

    /* Matrix ports */
    DDRA = 0xFF;
    DDRC = 0xFF;

    /* Key inputs + internal pull-ups */
    DDRD &= ~((1 << KEY1) | (1 << KEY2) | (1 << KEY3));
    PORTD |= (1 << KEY1) | (1 << KEY2) | (1 << KEY3);

    screen_clear();
    matrix_all_off();

    while (1)
    {
        /*
           Matrix refresh is continuous.
           This is the most important part for a multiplexed
           dot matrix: the screen must NOT stop refreshing.
        */
        matrix_refresh_1ms();

        /* 10 ms key sample */
        keyTimer++;

        if (keyTimer >= 10)
        {
            keyTimer = 0;

            command = key_command_10ms();

            if (command != 0)
                set_mode(command);
        }

        /* Animation clock */
        animationTime++;

        if (mode == 1)
        {
            if (animationTime >= 500)
            {
                animationTime = 0;

                animationIndex++;

                if (animationIndex >= 15)
                    animationIndex = 0;
            }

            draw_mode1(animationIndex);
        }
        else if (mode == 2)
        {
            if (animationTime >= 500)
            {
                animationTime = 0;

                animationIndex++;

                if (animationIndex >= 28)
                    animationIndex = 0;
            }

            draw_mode2(animationIndex);
        }
        else if (mode == 3)
        {
            if (animationTime >= 1000)
            {
                animationTime = 0;

                digit++;

                if (digit >= 10)
                    digit = 0;
            }

            draw_digit(digit);
        }
        else if (mode == 4)
        {
            if (animationTime >= 1000)
            {
                animationTime = 0;

                digit++;

                if (digit >= 10)
                    digit = 0;
            }

            draw_negative_digit(digit);
        }
        else if (mode == 5)
        {
            if (animationTime >= 1000)
            {
                animationTime = 0;

                digit++;

                if (digit >= 10)
                    digit = 0;
            }

            draw_rotated_digit(digit);
        }
        else if (mode == 6)
        {
            if (animationTime >= 500)
            {
                animationTime = 0;

                animationIndex++;

                /* 14 positions: 8,7,...,-5 */
                if (animationIndex >= 14)
                {
                    animationIndex = 0;
                    digit++;

                    if (digit >= 10)
                        digit = 0;
                }
            }

            draw_scroll_digit(digit, 8 - animationIndex);
        }

        /*
           matrix_refresh_1ms() already waits about 200 us.
           This additional 800 us makes one main-loop tick
           approximately 1 ms.
        */
        delay_us(800);
    }
}
