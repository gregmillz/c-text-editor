// includes
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ttycom.h>
#include <termios.h>
#include <unistd.h>

#define KILO_VERSION "0.0.1"

// defines
#define CTRL_KEY(k) ((k) & 0x1f)

// data
struct editorConfig {
  int screenrows;
  int screencols;
  struct termios orig_termios;
};

struct editorConfig E;

// terminal
void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
    die("tcsetattr");
  }
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
    die("tcgetattr");
  }
  atexit(disableRawMode);

  struct termios raw = E.orig_termios;

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

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  // this is the Device Status Report escape sequence.
  //
  // write it to stdout, we can read the byte sequence
  // afterwards
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
    return -1;
  }

  // the Device Status report will write the escape character 27
  // followed by [ then 18;97R. So we want to read into buf until R
  // is found.
  while (i < sizeof(buf) - 1) {
    // reads 1 byte into buf[i]
    if (read(STDIN_FILENO, &buf[i], 1) != 1) {
      break;
    }
    if (buf[i] == 'R') {
      break;
    }
    i++;
  }

  // printf expects strings to end with a 0 byte so assign \0
  // to the final byte of buf
  buf[i] = '\0';

  // ensure there's an escape sequence
  if (buf[0] != '\x1b' || buf[1] != '[') {
    return -1;
  }

  // pass the pointers to third character of buf so
  // we're passing a string like 24;80 to sscanf.
  // The string %d;%d tells it to parse two integers
  // separated by a ; and put the values into rows and cols
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) {
    return -1;
  }

  return 0;
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  // for some reason these can be zero
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    // that can sometimes fail, and if so, we can just handle this manually by
    // going to the bottom-right of the screen, then use escape sequences to
    // fine the position of the cursor to find how many rows and columns
    // there must be on the screen
    //
    // use the C command (Cursor Forward) to move to the right, then the B
    // command (Cursor Down) to move down
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
      return -1;
    }
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

// append buffer consisting of a pointer to the buffer,
// and length so we can read from the pointer onwards
struct abuf {
  char *b;
  int len;
};

// constructor for abuf type.
#define ABUF_INIT {NULL, 0}

// append string s to an abuf
void abAppend(struct abuf *ab, const char *s, int len) {
  // allocate enough memory to hold the new string. AKA
  // use realloc to resize the previously allocated heap block.
  //
  // realloc will return a block of memory of size of the current
  // string plus the size of the string to append.
  //
  // relloac will either extend the size of the block of memory
  // that is already allocated, or it will free() the current block
  // of memory and allocate a new block somewhere else that is big
  // enough for the string.
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL) {
    return;
  }

  // memcpy is from string.h
  // memcpy copies s after the end of the current data
  // in ab->b
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) {
  // free is from stdlib
  free(ab->b);
}

// output

void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    if (y == E.screenrows / 3) {
      char welcome[80];
      int welcomelen = snprintf(welcome, sizeof(welcome),
                                "Kilo editor -- version %s", KILO_VERSION);
      if (welcomelen > E.screencols) {
        welcomelen = E.screencols;
      }

      // center the welcome message
      int padding = (E.screencols - welcomelen) / 2;
      if (padding) {
        abAppend(ab, "~", 1);
        padding--;
      }
      // print the spaces leading up to welcome
      while (padding--) {
        abAppend(ab, " ", 1);
      }
      abAppend(ab, welcome, welcomelen);
    } else {
      abAppend(ab, "~", 1);
    }

    // wipe each line after the tilde by
    // using K which erases part of the line
    // to the right of the cursor
    abAppend(ab, "\x1b[K", 3);

    if (y < E.screenrows - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

// follows VT100 instructions https://vt100.net/docs/vt100-ug/chapter3.html#CUP
void editorRefreshScreen() {
  struct abuf ab = ABUF_INIT;

  // example of esc sequence:
  // \x is punctuation for "a hex byte follows"
  // byte 1: escape characters: 1b
  // bytes 2-4: [2J
  //
  // hide the cursor while drawing
  abAppend(&ab, "\x1b[?25l", 6);

  // place cursor at the top left with the H command
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  // another <esc>[H to reposition the cursor back to the top-left
  abAppend(&ab, "\x1b[H", 3);

  // show the cursor
  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
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
void initEditor() {
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
    die("getWindowSize");
  }
}

int main() {
  enableRawMode();
  initEditor();

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}
