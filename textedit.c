// includes
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

// defines
#define CTRL_KEY(k) ((k) & 0x1f)

// data
struct termios orig_termios;

// terminal
void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
    die("tcsetattr");
  }
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
    die("tcgetattr");
  }
  atexit(disableRawMode);

  struct termios raw = orig_termios;

  raw.c_iflag &= ~(BRKINT | ICRNL | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    die("tcsetaddr");
  }
}

// wait for one keypress and return it
char editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) {
      die("read");
    }
  }
  return c;
}

// output

// follows VT100 instructions https://vt100.net/docs/vt100-ug/chapter3.html#CUP
void editorRefreshScreen() {
  // writes an escape sequence (4 bytes).
  // Escape sequences start with an escape character,
  // followed by a [ character.
  //
  // \x is punctuation for "a hex byte follows"
  // byte 1: escape characters: 1b
  // bytes 2-4: [2J
  //
  // J is a command to clear the screen. Escape sequence
  // commands take arguments which come before the command, 2 in
  // this case, to clear the screen.
  write(STDOUT_FILENO, "\x1b[2J", 4);

  // place cursor at the top right with the H command
  write(STDOUT_FILENO, "\x1b[H", 3);
}

// input

// wait for a keypress then handle it
void editorProcessKeypress() {
  char c = editorReadKey();

  switch (c) {
  case CTRL_KEY('q'):
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;
  }
}

// init
int main() {
  enableRawMode();

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}
