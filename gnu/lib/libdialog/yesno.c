/*
 *  yesno.c -- implements the yes/no box
 *
 *  AUTHOR: Savio Lam (lam836@cs.cuhk.hk)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <dialog.h>
#include "dialog.priv.h"


/*
 * Display a dialog box with two buttons - Yes and No
 */
int dialog_yesno(unsigned char *title, unsigned char * prompt, int height, int width)
{
  int i, j, x, y, key = 0, button = 0;
  WINDOW *dialog;

  if (height < 0)
	height = strheight(prompt)+4;
  if (width < 0) {
	i = strwidth(prompt);
	j = strwidth(title);
	width = MAX(i,j)+4;
  }

  /* center dialog box on screen */
  x = (COLS - width)/2;
  y = (LINES - height)/2;
  
#ifdef HAVE_NCURSES
  if (use_shadow)
    draw_shadow(stdscr, y, x, height, width);
#endif
  dialog = newwin(height, width, y, x);
  if (dialog == NULL) {
    endwin();
    fprintf(stderr, "\nnewwin(%d,%d,%d,%d) failed, maybe wrong dims\n", height,width,y,x);
    exit(1);
  }
  keypad(dialog, TRUE);

  draw_box(dialog, 0, 0, height, width, dialog_attr, border_attr);
  wattrset(dialog, border_attr);
  wmove(dialog, height-3, 0);
  waddch(dialog, ACS_LTEE);
  for (i = 0; i < width-2; i++)
    waddch(dialog, ACS_HLINE);
  wattrset(dialog, dialog_attr);
  waddch(dialog, ACS_RTEE);
  wmove(dialog, height-2, 1);
  for (i = 0; i < width-2; i++)
    waddch(dialog, ' ');

  if (title != NULL) {
    wattrset(dialog, title_attr);
    wmove(dialog, 0, (width - strlen(title))/2 - 1);
    waddch(dialog, ' ');
    waddstr(dialog, title);
    waddch(dialog, ' ');
  }
  wattrset(dialog, dialog_attr);
  wmove(dialog, 1, 2);
  print_autowrap(dialog, prompt, height-1, width-2, width, 1, 2, TRUE, FALSE);

  x = width/2-10;
  y = height-2;
  print_button(dialog, "  No  ", y, x+13, FALSE);
  print_button(dialog, " Yes ", y, x, TRUE);
  wrefresh(dialog);

  while (key != ESC) {
    key = wgetch(dialog);
    switch (key) {
      case 'Y':
      case 'y':
        delwin(dialog);
        return 0;
      case 'N':
      case 'n':
        delwin(dialog);
        return 1;
      case KEY_BTAB:
      case TAB:
      case KEY_UP:
      case KEY_DOWN:
      case KEY_LEFT:
      case KEY_RIGHT:
        if (!button) {
          button = 1;    /* Indicates "No" button is selected */
          print_button(dialog, " Yes ", y, x, FALSE);
          print_button(dialog, "  No  ", y, x+13, TRUE);
        }
        else {
          button = 0;    /* Indicates "Yes" button is selected */
          print_button(dialog, "  No  ", y, x+13, FALSE);
          print_button(dialog, " Yes ", y, x, TRUE);
        }
        wrefresh(dialog);
        break;
      case ' ':
      case '\r':
      case '\n':
        delwin(dialog);
        return button;
      case ESC:
        break;
    }
  }

  delwin(dialog);
  return -1;    /* ESC pressed */
}
/* End of dialog_yesno() */
