#ifndef EDGESTASH_TTL_DAEMON_H
#define EDGESTASH_TTL_DAEMON_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

bool TTLDaemon_Start(HWND hwndNotify);
void TTLDaemon_Stop(void);
void TTLDaemon_TriggerImmediatePurge(void);

#ifdef __cplusplus
}
#endif

#endif // EDGESTASH_TTL_DAEMON_H
