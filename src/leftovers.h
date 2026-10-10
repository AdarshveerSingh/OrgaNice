/*
 * leftovers.h - Feature 2: finds ZIPs that were already extracted,
 * installers whose program is already installed (or that a newer
 * installer replaced), and unfinished downloads.
 */
#ifndef ORGANICE_LEFTOVERS_H
#define ORGANICE_LEFTOVERS_H

void leftovers_run(const char *preset_root, int advanced);

#endif
