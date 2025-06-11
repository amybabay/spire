#ifndef CONFIG_UTILS_H
#define CONFIG_UTILS_H

#include <openssl/evp.h>
#include "parser.h"

char *read_file_as_string(const char *filepath);
void generate_all_site_tc_keys(int req_shares, int faults, int rej_servers);
void load_threshold_pubkeys(struct config *cfg);
void generate_keys_for_host(struct host *host);
void generate_keys_for_replica(struct replica *replica, struct host *host, unsigned site_index);
void generate_keys_for_client(struct client *client, struct host *host);
void generate_keys(struct config *cfg);
struct config *load_and_process_config(const char *input_yaml, int simulate_tpm);
int load_config_manager_keys(EVP_PKEY **priv_key, EVP_PKEY **pub_key);
int is_hmi(unsigned client_id, struct config *cfg);

#endif // CONFIG_UTILS_H
