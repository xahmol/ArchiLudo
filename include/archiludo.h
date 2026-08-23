#ifndef ARCHILUDO_H
#define ARCHILUDO_H

#include "oslib/wimp.h"

#define APP_NAME "ArchiLudo"

extern wimp_t task_handle;

void archiludo_initialise(const char *argv0);
void archiludo_poll_loop(void);

#endif
