#include <ncurses.h>

int main() {
    initscr();
    printw("TUI TEST OK!\n");
    refresh();
    getch();
    endwin();
    return 0;
}

