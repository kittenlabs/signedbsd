/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@login.dknet.dk> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $Id$
 *
 */

#include "ctm.h"

int
ctm_edit(u_char *script, int length, char *filein, char *fileout)
{
    u_char *ep, cmd;
    int ln, ln2, iln, ret=0, c;
    FILE *fi=0,*fo=0;

    fi = fopen(filein,"r");
    if(!fi) { 
	perror(filein);
	return 8; 
    }

    fo = fopen(fileout,"w");
    if(!fo) {
	perror(fileout);
	fclose(fi);
	return 4;
    }
    iln = 0;
    for(ep=script;ep < script+length;) {
	cmd = *ep++;
	if(cmd != 'a' && cmd != 'd') { ret = 1; goto bye; }
	ln = 0;
	while(isdigit(*ep)) {
	    ln *= 10;
	    ln += (*ep++ - '0');
	}
	if(*ep++ != ' ') { ret = 1; goto bye; }
	ln2 = 0;
	while(isdigit(*ep)) {
	    ln2 *= 10;
	    ln2 += (*ep++ - '0');
	}
	if(*ep++ != '\n') { ret = 1; goto bye; }
	while(iln < ln) {
	    c = getc(fi);
	    putc(c,fo);
	    if(c == '\n')
		iln++;
	}
	if(cmd == 'd') {
	    while(ln2) {
		c = getc(fi);
		if(c != '\n')
		    continue;
		iln++;
		ln2--;
	    }
	    continue;
	}
	if(cmd == 'a') {
	    while(ln2) {
		c = *ep++;
		putc(c,fo);
		if(c != '\n')
		    continue;
		ln2--;
	    }
	    continue;
	}
	ret = 1; 
	goto bye;
    }
    while(1) {
	c = getc(fi);
	if(c == EOF) break;
	putc(c,fo);
    }
    fclose(fi);
    fclose(fo);
    return 0;
bye:
    if(fi) 	fclose(fi);
    if(fo) 	fclose(fo);
    return ret;
}

