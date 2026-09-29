#include "morse.h"

/* International Morse code. The order of this table is also the order of
 * the mastery grid and of the 5x7 pixel font below. */
const MorseChar morse_table[CHAR_COUNT] = {
    {'A', ".-"},     {'B', "-..."},   {'C', "-.-."},   {'D', "-.."},    {'E', "."},
    {'F', "..-."},   {'G', "--."},    {'H', "...."},   {'I', ".."},     {'J', ".---"},
    {'K', "-.-"},    {'L', ".-.."},   {'M', "--"},     {'N', "-."},     {'O', "---"},
    {'P', ".--."},   {'Q', "--.-"},   {'R', ".-."},    {'S', "..."},    {'T', "-"},
    {'U', "..-"},    {'V', "...-"},   {'W', ".--"},    {'X', "-..-"},   {'Y', "-.--"},
    {'Z', "--.."},   {'0', "-----"},  {'1', ".----"},  {'2', "..---"},  {'3', "...--"},
    {'4', "....-"},  {'5', "....."},  {'6', "-...."},  {'7', "--..."},  {'8', "---.."},
    {'9', "----."},  {'.', ".-.-.-"}, {',', "--..--"}, {'?', "..--.."}, {'/', "-..-."},
    {'=', "-...-"},
};

/* Lesson order: contrasting pairs (E/T, I/M, A/N, S/O ...) are easiest to
 * tell apart, and the early letters already form lots of real words. */
const LessonDef lessons[LESSON_COUNT] = {
    {"ET", "E is one short beep (dit). T is one long beep (dah)."},
    {"IM", "I is two dits, M is two dahs - a double E and T."},
    {"AN", "A is di-dah, N is dah-dit. They are mirror images."},
    {"SO", "S is three dits, O is three dahs. Now you can send SOS!"},
    {"RK", "R is di-dah-dit, K is dah-di-dah: dits and dahs swapped."},
    {"DU", "D is dah-di-dit, U is di-di-dah. Another mirror pair."},
    {"GW", "G is dah-dah-dit, W is di-dah-dah. Feel the rhythm."},
    {"HB", "H is four quick dits. B is one dah, then three dits."},
    {"LF", "L is di-dah-di-dit, F is di-di-dah-dit. Four parts each."},
    {"CY", "C is dah-di-dah-dit, Y is dah-di-dah-dah. Both start like K."},
    {"PX", "P is di-dah-dah-dit, X is dah-di-di-dah. Mirror twins."},
    {"VZ", "V is di-di-di-dah (Beethoven's 5th!). Z is dah-dah-di-dit."},
    {"JQ", "J is dit + three dahs, Q is dah-dah-di-dah. Alphabet done!"},
    {"123", "Numbers have 5 parts: 1 = 1 dit + 4 dahs, 2 = 2 dits + 3 dahs."},
    {"456", "4 = four dits + dah, 5 = five dits, 6 = dah + four dits."},
    {"7890", "7 = 2 dahs + 3 dits, 8 = 3 dahs, 9 = 4 dahs, 0 = five dahs."},
    {".,", "Period: di-dah three times. Comma: dah-dah-di-di-dah-dah."},
    {"?/=", "? is di-di-dah-dah-di-dit, / is dah-di-di-dah-dit, = is a break."},
    {"", "The final exam mixes all signs, words and numbers. Go!"},
};

const BadgeDef badges[BADGE_COUNT] = {
    {"First Steps", "Finish lesson 1"},
    {"Save Our Souls", "Learn S and O"},
    {"Alphabet", "Learn all letters"},
    {"Number Cruncher", "Learn all numbers"},
    {"Graduate", "Pass the final exam"},
    {"Perfectionist", "3 stars in a lesson"},
    {"On Fire", "3 day streak"},
    {"Unstoppable", "7 day streak"},
    {"Sharp Ear", "20 right in a row"},
    {"Wordsmith", "50 words correct"},
    {"Rush Hero", "100 pts in Morse Rush"},
    {"Speedster", "Session at 20+ WPM"},
};

/* XP needed per level; see level_floor_xp() in morse_main.c */
const uint8_t daily_goals[4] = {20, 50, 100, 150};

static const char* const level_titles[] = {
    "Beginner",
    "Listener",
    "Tapper",
    "Sparks",
    "Operator",
    "Radio Pro",
    "Ace",
    "Veteran",
    "Master",
    "Legend",
};

const char* level_title(uint8_t level) {
    uint8_t count = sizeof(level_titles) / sizeof(level_titles[0]);
    if(level < 1) level = 1;
    if(level > count) level = count;
    return level_titles[level - 1];
}

/* Common English words (max 6 letters so they fit the answer boxes). Only
 * words made of already learned letters are ever used. */
const char* const word_list[] = {
    /* E T I M A N - available from lesson 3 */
    "AT",    "IN",    "IT",    "AN",    "ME",    "MA",    "TIE",   "TIME",  "ITEM",  "EMIT",
    "MET",   "MEN",   "MAN",   "TEN",   "NET",   "TEA",   "EAT",   "ATE",   "TIN",   "TAN",
    "MAT",   "ANT",   "AIM",   "INN",   "TAME",  "TEAM",  "MEAT",  "MEAN",  "NAME",  "MINE",
    "MINT",  "MAIN",  "TENT",  "NINE",  "MANE",  "ANTE",  "MEANT", "INMATE",
    /* + S O */
    "SO",    "NO",    "ON",    "TO",    "AS",    "IS",    "SIT",   "SAT",   "SET",   "SEA",
    "SON",   "TOE",   "NOT",   "TOO",   "SEE",   "MOM",   "NOSE",  "NOTE",  "MOST",  "MOON",
    "SOON",  "SEAT",  "EAST",  "SAME",  "SIMON", "STONE", "TOAST", "MOTION","SEASON","MASON",
    "OMEN",  "MIST",  "TOES",  "ONION", "SNOW",
    /* + R K D U G W H */
    "RED",   "RUN",   "SUN",   "KEY",   "DOG",   "DAD",   "DID",   "GO",    "DO",    "US",
    "UP",
    "OUR",   "OUT",   "WAS",   "WHO",   "HOW",   "NOW",   "NEW",   "HIS",   "HER",   "HAS",
    "HAD",   "HIM",   "SHE",   "THE",   "AND",   "ARE",   "WE",    "HE",    "OR",    "DUE",
    "HOME",  "WORD",  "WORK",  "DOOR",  "WIND",  "KING",  "GOOD",  "ROAD",  "RAIN",  "STAR",
    "HAND",  "HEAD",  "HEART", "WATER", "HORSE", "HOUSE", "RADIO", "MORSE", "SIGNAL","SOUND",
    "NIGHT", "WORDS", "SHORT", "DANGER","SHIRT", "THREE", "DREAM", "GREAT", "SMART", "TOWER",
    "RADAR", "EARTH", "STORM", "OCEAN", "SHORE", "WAGON", "DRUM",  "GUARD", "HONEST",
    /* + B L F C Y P X V Z J Q */
    "BIG",   "BED",   "BOX",   "FOX",   "FLY",   "SKY",   "CAT",   "CAR",   "CUP",   "MAP",
    "TOP",   "HOT",   "POT",   "YES",   "YOU",   "JOB",   "JAM",   "ZOO",   "ICE",   "VAN",
    "LOVE",  "LIFE",  "BOOK",  "FISH",  "BIRD",  "TREE",  "SHIP",  "BOAT",  "LAMP",  "HELP",
    "CALL",  "CODE",  "SEND",  "WAVE",  "BEAM",  "LOUD",  "FAST",  "SLOW",  "TEST",  "ZERO",
    "ZONE",  "QUIZ",  "JUMP",  "JOKE",  "VIEW",  "FIVE",  "SEVEN", "SIX",   "MIX",   "EXIT",
    "LIGHT", "RIVER", "MONEY", "MUSIC", "PAPER", "TABLE", "CHAIR", "SMILE", "HAPPY", "QUICK",
    "BROWN", "JUMPS", "LAZY",  "OVER",  "QUEEN", "VOICE", "QUIET", "EXTRA", "JUICE", "PLANE",
    "PILOT", "CLOUD", "BEACON","BRIDGE","FLIGHT","PLANET","ROCKET","CASTLE","PUZZLE","SPIRIT",
    "COFFEE","CIRCLE","VALLEY","JUNGLE","BLOCK", "CLOCK", "FLAME", "BRAVE", "GLOBE", "OXYGEN",
};
const uint16_t word_count = sizeof(word_list) / sizeof(word_list[0]);

/* 5x7 pixel font for the large letters, one byte per row, bit 4 = left. */
const uint8_t glyph_5x7[CHAR_COUNT][7] = {
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* A */
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, /* B */
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, /* C */
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}, /* D */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, /* E */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, /* F */
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, /* G */
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* H */
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* I */
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, /* J */
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, /* K */
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, /* L */
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, /* M */
    {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}, /* N */
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* O */
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, /* P */
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, /* Q */
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, /* R */
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, /* S */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, /* T */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* U */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, /* V */
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, /* W */
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, /* X */
    {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}, /* Y */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, /* Z */
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, /* 0 */
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* 1 */
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, /* 2 */
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}, /* 3 */
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, /* 4 */
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, /* 5 */
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, /* 6 */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, /* 7 */
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, /* 8 */
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, /* 9 */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}, /* . */
    {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}, /* , */
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}, /* ? */
    {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00}, /* / */
    {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}, /* = */
};

int morse_index(char c) {
    if(c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    for(int i = 0; i < CHAR_COUNT; i++) {
        if(morse_table[i].ch == c) return i;
    }
    return -1;
}

const char* morse_code(char c) {
    int i = morse_index(c);
    return i < 0 ? "" : morse_table[i].code;
}

char morse_decode(const char* pattern) {
    if(!pattern || !pattern[0]) return 0;
    for(int i = 0; i < CHAR_COUNT; i++) {
        if(strcmp(morse_table[i].code, pattern) == 0) return morse_table[i].ch;
    }
    return 0;
}
