/*
 * Character Type Functions - VibeOS Compatibility  
 * 
 * Essential for text processing utilities like wc, grep, sed
 */

#ifndef COMPAT_CTYPE_H
#define COMPAT_CTYPE_H

int isalpha(int c);
int isdigit(int c);
int isalnum(int c);
int isspace(int c);
int isupper(int c);
int islower(int c);
int ispunct(int c);
int isprint(int c);
int isgraph(int c);
int iscntrl(int c);
int isxdigit(int c);

int tolower(int c);
int toupper(int c);

#endif /* COMPAT_CTYPE_H */
