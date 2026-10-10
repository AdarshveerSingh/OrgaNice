/*
 * OrgaNice - a safe file organizer and cleanup tool written in C.
 *
 * Features: organize by type/extension/name/size/date, content-based
 * duplicate detection with hard-link replacement, detection of already
 * extracted ZIPs and already installed installers, archiving of old files,
 * and full undo for every operation.
 */
#include "platform.h"
#include "ui.h"

int main(void)
{
    plat_console_init();
    ui_run();
    return 0;
}
