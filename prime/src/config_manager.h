#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "parser.h"  // needed for struct config

void decrypt_all_private_keys(struct config *cfg);
struct host *find_host_for_replica(struct site *site, const char *host_name);

#endif // CONFIG_MANAGER_H
