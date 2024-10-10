#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>

/****** STUFF ******/
#define QUIT_TIMES 2
/****** HM MHHHM ******/

void setStatusMessage(const char *fmt, ...);
void RefreshScreen();
char *prompt(char *prompt);

/****** GLOBAL STATE ******/
typedef struct erow {
    int size;
    int rsize;
    char *chars, *render;
} erow;
struct ConfigEditor {
    int cX, cY;
    int renderX;
    int screenrows, screencols;
    struct termios o_termios;
    int nRows;
    int row_offset, col_offset;
    char *filename;
    int zozzo;
    char statusMSG[80];
    time_t SMSGT; // status message time......
    erow *row;
};
struct ConfigEditor E;

/****** APPEND BUFFER ******/

struct abuff {
    char *b;
    int len;
};

#define ABUFF_INIT {NULL, 0}

void abAppend(struct abuff *ab, const char *s, int len) {
    char *newb = realloc(ab->b, ab->len + len);

    if (newb == NULL) return;
    memcpy(&newb[ab->len], s, len);
    ab->b = newb;
    ab->len += len;
}

void abFree(struct abuff *ab) {
    free(ab->b);
}
/****** CTRL KEY COMBO ******/
#define CTRL_KEY(k) ((k) & 0x1f)

/****** MORE KEYS ******/

#define TAB_STOP 8
enum editorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 10000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    HOME,
    END,
    DEL
};
/*--------------------------*/

void die(const char *e) {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
	perror(e);
	exit(1);
}

void dRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.o_termios) == -1) die("tcsetattr");
}

void eRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.o_termios) == -1) die("tcgetattr");
    atexit(dRawMode);
    struct termios raw = E.o_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int getCursorPosition(int *rows, int *cols) {
    char buff[32];
    unsigned int i = 0;
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
    while (i < sizeof(buff) - 1) {
        if (read(STDIN_FILENO, &buff[i], 1) != 1) break;
        if (buff[i] == 'R') break;
        i++;
    }
    buff[i] = '\0';
    if (buff[0] != '\x1b' || buff[1] != '[') return -1;
    if (sscanf(&buff[2], "%d;%d", rows, cols) != 2) return -1;
    return 0;
    }

int getWindowSize(int *rows, int *cols) {
    
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}
/****** INPUT ******/
int ReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1':
                        case '7':
                            return HOME;
                        case '4':
                        case '8': 
                            return END;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '3': return DEL;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME;
                    case 'F': return END;
                }
            }
        } else if (seq [0] == '0') {
            switch (seq[1]){
                case 'H': return HOME;
                case 'F': return END;
            }
        }
        return '\x1b';
    } else {
        return c;
    }
}

void ProcessKeyPress() {
    static int quit_times = QUIT_TIMES;

    int c = ReadKey();
    switch (c) {
        case '\r':
            insertNewline();
            break;
        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL:
            if (c == DEL) MoveCursor(ARROW_RIGHT);
            delChar();
            break;
        case CTRL_KEY('l'):
        case '\x1b':
            break;
        case CTRL_KEY('s'):
            Save();
            break;
        case CTRL_KEY('q'):
            if (E.zozzo && quit_times > 0) {
                setStatusMessage("!! File has unsaved changes. Press ^Q %d more times to quit", quit_times);
                quit_times--;
                return;
            }
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            MoveCursor(c);
            break;
        case PAGE_DOWN:
        case PAGE_UP:
            {
                if (c == PAGE_UP) E.cY = E.row_offset;
                else if (c == PAGE_DOWN) {
                    E.cY = E.row_offset + E.screenrows - 1;
                    if (E.cY > E.nRows) E.cY = E.nRows;
                }

                int lines = E.screenrows;
                while (lines--) MoveCursor(c == PAGE_UP?ARROW_UP:ARROW_DOWN);
            }
            break;
        case HOME:
            E.cX = 0;
            break;
        case END:
            if (E.cY < E.nRows)
                E.cX = E.row[E.cY].size;
            break;
        default:
            insertChar(c);
            break;
    }
    quit_times = QUIT_TIMES;
}

void MoveCursor(int key) {
    erow *row = (E.cY >= E.nRows) ?NULL:&E.row[E.cY];
    switch (key) {
        case ARROW_LEFT:
            if (E.cX != 0) E.cX--;
            else if (E.cY > 0) {
                E.cY--;
                E.cX = E.row[E.cY].size;
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cX < row->size)
                E.cX++;
            else if(row && E.cX == row->size) {
                E.cY++;
                E.cX = 0;
            }
            break;
        case ARROW_UP:
            if (E.cY != 0) E.cY--;
            break;
        case ARROW_DOWN:
            if (E.cY < E.nRows) E.cY++;
            break;
    }

    row = (E.cY >= E.nRows?NULL:&E.row[E.cY]);
    int rowlen = row?row->size:0;
    if (E.cX > rowlen) E.cX = rowlen;
}

/****** GUI MANAGEMENT ******/
int convertCtRX(erow *row, int cx) {
    int rx = 0;
    int i;
    for (i = 0; i < cx; i++) {
        if (row->chars[i] == '\t')
            rx += (TAB_STOP - 1) - (rx%TAB_STOP);
        rx++;
    }
    return rx;
}

void Scroll() {
    E.renderX = 0;
    if (E.cY < E.nRows)
        E.renderX = convertCtRX(&E.row[E.cY], E.cX);

    if (E.cY < E.row_offset) 
        E.row_offset = E.cY;
    if (E.cY >= E.row_offset + E.screenrows) 
        E.row_offset = E.cY - E.screenrows + 1;
    if (E.renderX < E.col_offset)
        E.col_offset = E.renderX;
    if (E.renderX >= E.col_offset + E.screencols)
        E.col_offset = E.renderX - E.screencols + 1;
}

void RefreshScreen() {
    Scroll();
    
    struct abuff ab = ABUFF_INIT; 

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    DrawRows(&ab);
    drawStatusBar(&ab);
    drawMessageBar(&ab, 5);

    char buff[32];
    snprintf(buff, sizeof(buff), "\x1b[%d;%dH", E.cY - E.row_offset + 1, E.renderX - E.col_offset + 1);
    abAppend(&ab, buff, strlen(buff));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void DrawRows(struct abuff *ab) {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        int filerow = y + E.row_offset;
        if (filerow >= E.nRows) {
            abAppend(ab, "~", 1);
        } else {
            int len = E.row[filerow].rsize - E.col_offset;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;
            abAppend(ab, &E.row[filerow].render[E.col_offset], len);
        }
        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n",2);
    }
}

void drawStatusBar(struct abuff *ab) {
    abAppend(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", (E.filename?E.filename:"[No Name]"), E.nRows, E.zozzo?"(Modified)":"");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cY + 1, E.nRows);
    if (len > E.screencols) len = E.screencols;
    abAppend(ab, status, len);
    while (len < E.screencols) {
        if (E.screencols - len == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        } else {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void setStatusMessage(const char *fmt, ...){
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusMSG, sizeof(E.statusMSG), fmt, ap);
    va_end(ap);
    E.SMSGT = time(NULL);
} 

void drawMessageBar(struct abuff *ab, int tsec) {
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusMSG);
    if (msglen > E.screencols) msglen = E.screencols;
    if (msglen && time(NULL) - E.SMSGT < tsec)
        abAppend(ab, E.statusMSG, msglen);
}

char *prompt(char *prompt) {
    size_t buffsize = 128;
    char *buf = malloc(buffsize);

    size_t bufflen = 0;
    buf[0] = '\0';

    while (1) {
        setStatusMessage(prompt, buf);
        RefreshScreen();
        int c = ReadKey();
        if (c == DEL || c == CTRL_KEY('h') || c == BACKSPACE) {
            if (bufflen != 0)
                buf[--bufflen] = '\0';
        } else if (c == '\x1b') {
            setStatusMessage("");
            free(buf);
            return NULL;
        } else if (c == '\r') {
            if (bufflen != 0) {
                setStatusMessage("");
                return buf;
            }
        } else if (!iscntrl(c) && c < 128) {
            if (bufflen == buffsize - 1) {
                buffsize *= 2;
                buf = realloc(buf, buffsize);
            }
            buf[bufflen++] = c;
            buf[bufflen] = '\0';
        }

    }

}

/****** THE EDITOR ******/
void updateRow(erow *row) {
    int tabs = 0;
    int i;
    for (i = 0; i < row->size; i++)
        if (row->chars[i] == '\t') tabs++;
    
    free(row->render);
    row->render = malloc(row->size + tabs*(TAB_STOP - 1) + 1);
    int indx = 0;
    for (i = 0; i < row->size; i++) 
        if (row->chars[i] == '\t') {
            row->render[indx++] = ' ';
            while (indx % TAB_STOP != 0) row->render[indx++] = ' ';
        } else 
            row->render[indx++] = row->chars[i];

    
    row->render[indx] = '\0';
    row->rsize = indx;
}

void insertRow(int a, char *s, size_t len) {
    if (a < 0 || a > E.nRows) return;
    E.row = realloc(E.row, sizeof(erow) * (E.nRows + 1));
    memmove(&E.row[a + 1], &E.row[a], sizeof(erow) * (E.nRows - a));
    
    E.row[a].size = len;
    E.row[a].chars = malloc(len + 1);
    memcpy(E.row[a].chars, s, len);
    E.row[a].chars[len] = '\0';

    E.row[a].rsize = 0;
    E.row[a].render = NULL;
    updateRow(&E.row[a]);

    E.nRows++;
    E.zozzo++;
}

void rowInsertChar(erow *row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size;
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at+1], &row->chars[at], row->size -at +1);
    row->size++;
    row->chars[at] = c;
    updateRow(row);
    E.zozzo++;
}

void insertChar(int c) {
    if (E.cY == E.nRows) insertRow(E.nRows, "", 0);
    rowInsertChar(&E.row[E.cY], E.cX, c);
    E.cX++;
}

void insertNewline() {
    if (E.cX == 0)
        insertRow(E.cY, "", 0);
    else {
        erow * row = &E.row[E.cY];
        insertRow(E.cY + 1, &row->chars[E.cX], row->size - E.cX);
        row = &E.row[E.cY];
        row->size = E.cX;
        row->chars[row->size] = '\0';
        updateRow(row);
    }
    E.cY++;
    E.cX = 0;
}

void rowDelChar(erow *row, int at) {
    if (at < 0 || at >= row->size) return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    updateRow(row);
    E.zozzo++;
}

void delChar() {
    if (E.cY >= E.nRows) return;
    if (E.cX == 0 && E.cY == 0) return;
    erow *row = &E.row[E.cY];
    if (E.cX > 0) {
        rowDelChar(row, E.cX - 1);
        E.cX--;
    } else {
        E.cX = E.row[E.cY - 1].size;
        appendString(&E.row[E.cY - 1], row->chars, row->size);
        delRow(E.cY);
        E.cY--;
    }
}

void freeRow(erow *row) {
    free(row->render);
    free(row->chars);
}

void delRow(int at) {
    if (at < 0 || at >= E.nRows) return;
    freeRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.nRows - at - 1));
    E.nRows--;
    E.zozzo++;
}

appendString(erow *row, char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    row->size += len;
    row->chars[row->size] = '\0';
    updateRow(row);
    E.zozzo++;
}

char *rowsToString(int *bufflen) {
    int totlen = 0;
    int i;
    for (i = 0; i < E.nRows; i++)
        totlen += E.row[i].size + 1;
    *bufflen = totlen;

    char *buff = malloc(totlen);
    char *p = buff;
    for (i = 0; i < E.nRows; i++) {
        memcpy(p, E.row[i].chars, E.row[i].size);
        p += E.row[i].size;
        *p = '\n';
        p++;
    }
    return buff;
}

void FileOpen(char *filename) {
    free(E.filename);
    E.filename = strdup(filename);

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        Save();
        return;
    }
    // die("fopen"); //old code

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    
    while ((linelen = getline(&line, &linecap, fp))!= -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
            linelen--;
        insertRow(E.nRows, line, linelen);
    }
    

    free(line);
    fclose(fp);
    E.zozzo = 0;
}

void Save() {
    if (E.filename == NULL) {
        E.filename = prompt("Save as: %s [ESC to cancel]");
        if (E.filename == NULL) {
            setStatusMessage("Save Aborted.");
            return;
        }

    }

    int len;
    char *buff = rowsToString(&len);
    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buff, len) == len) {
                close(fd);
                free(buff);
                E.zozzo = 0;
                setStatusMessage("%d Bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }

    free(buff);
    setStatusMessage("Cannot save. Error %s", strerror(errno));
}

/*----------------------*/

void eInit(){
    E.cX = 0;
    E.cY = 0;
    E.renderX = 0;
    E.row_offset = 0;
    E.col_offset = 0;
    E.nRows = 0;
    E.row = NULL;
    E.filename = NULL;
    E.statusMSG[0] = '\0';
    E.SMSGT = 0;
    E.zozzo = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
    E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
    eRawMode();
    eInit();
    if (argc >= 2) {
        FileOpen(argv[1]);
    }

    setStatusMessage("^Q: Quit | ^S: Save");

    while (1) {
        RefreshScreen();
        ProcessKeyPress();
    }
    return 0;
}