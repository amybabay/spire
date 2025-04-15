#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "parser.h"  // needed for struct config

void decrypt_all_private_keys(struct config *cfg);

#endif // CONFIG_MANAGER_H
