/*
 * organize.h - Feature 1: sorting files into folders.
 */
#ifndef ORGANICE_ORGANIZE_H
#define ORGANICE_ORGANIZE_H

typedef enum {
    ORGANIZE_QUICK,      /* by file type, sensible defaults               */
    ORGANIZE_BY_DATE,    /* Week1/Week2, month, year... (+ optional type)  */
    ORGANIZE_ADVANCED    /* filters, name rules, size, custom folders      */
} OrganizeStyle;

/* preset_root: folder to work on, or NULL to ask the user. */
void organize_run(const char *preset_root, OrganizeStyle style);

#endif
