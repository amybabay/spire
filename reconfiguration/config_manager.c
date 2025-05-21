#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <openssl/sha.h>
#include <openssl/bn.h>
#include "tc_wrapper.h"
#include "parser.h"
#include "key_generation.h"
#include "../prime/OpenTC-1.1/TC-lib-1.0/TC.h"

#define SM_TC_DIR "tc_keys/sm/"
#define PRIME_TC_DIR "tc_keys/prime/"

/**
 * Reads the entire contents of a file into a null-terminated string.
 *
 * Opens the file at the given path, reads its contents into a newly allocated
 * buffer, and null-terminates it. The caller is responsible for freeing the buffer.
 *
 * @param filepath Path to the file to read.
 * @return Pointer to the allocated string, or NULL on failure.
 */
char *read_file_as_string(const char *filepath)
{
    FILE *fp = fopen(filepath, "r");
    if (!fp)
        return NULL;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    char *buffer = malloc(size + 1);
    if (!buffer)
    {
        fclose(fp);
        return NULL;
    }

    fread(buffer, 1, size, fp);
    buffer[size] = '\0';
    fclose(fp);
    return buffer;
}

/* Second pass: Generate all keys using the permanent public key */

/**
 * Generates threshold cryptography key shares for all sites.
 *
 * For each site, generates separate sets of threshold keys for both
 * Scada Masters and Prime replicas, and writes them to appropriate directories.
 *
 * @param req_shares   Number of shares to generate per site.
 * @param faults       Maximum number of tolerated faults (f).
 * @param rej_servers  Number of servers allowed to reject messages (k).
 * @param num_sites    Number of sites to generate keys for.
 */
void generate_all_site_tc_keys(int req_shares, int faults, int rej_servers, int num_sites)
{
    int n = 3 * faults + 2 * rej_servers + 1;
    int k = req_shares;
    int keysize = 1024;

    for (int site_id = 1; site_id <= num_sites; site_id++)
    {
        TC_DEALER *dealer_sm = TC_generate(keysize / 2, n, k, 17);
        TC_write_shares(dealer_sm, "tc_keys/sm", site_id);
        TC_DEALER_free(dealer_sm);

        TC_DEALER *dealer_prime = TC_generate(keysize / 2, n, k, 17);
        TC_write_shares(dealer_prime, "tc_keys/prime", site_id);
        TC_DEALER_free(dealer_prime);
    }
}

/**
 * Loads threshold public keys for Scada Master and Prime into config.
 *
 * Reads the SM and Prime threshold public key files (assumed to be named `pubkey_1.pem`)
 * from their respective directories and stores the contents as strings in the config.
 *
 * @param cfg Pointer to the config structure to populate.
 *
 * @note Exits the program if either key file fails to load.
 */
void load_threshold_pubkeys(struct config *cfg)
{
    char sm_pubkey_path[256];
    char prime_pubkey_path[256];

    snprintf(sm_pubkey_path, sizeof(sm_pubkey_path), "%spubkey_1.pem", SM_TC_DIR);
    snprintf(prime_pubkey_path, sizeof(prime_pubkey_path), "%spubkey_1.pem", PRIME_TC_DIR);

    cfg->service_keys.sm_threshold_public_key = read_file_as_string(sm_pubkey_path);
    cfg->service_keys.prime_threshold_public_key = read_file_as_string(prime_pubkey_path);

    if (!cfg->service_keys.sm_threshold_public_key || !cfg->service_keys.prime_threshold_public_key)
    {
        fprintf(stderr, "Error: Failed to read SM or Prime threshold public keys.\n");
        free_yaml_config(&cfg);
        exit(EXIT_FAILURE);
    }
}

/**
 * Generates and encrypts internal and external RSA key pairs for a host.
 *
 * Uses the host's TPM public key to hybrid-encrypt newly generated 2048-bit RSA
 * private keys for internal and external use. Stores the public keys and encrypted
 * private keys in the host struct.
 *
 * @param host Pointer to the host structure to populate with key material.
 *
 * @note Requires host->permanent_public_key to be set with a PEM-encoded key.
 *       Logs errors and returns early on failure.
 */
void generate_keys_for_host(struct host *host)
{
    if (!host->permanent_public_key)
    {
        fprintf(stderr, "Error: TPM public key missing for host %s\n", host->name);
        return;
    }

    EVP_PKEY *tpm_pubkey = load_public_key_from_pem(host->permanent_public_key);
    if (!tpm_pubkey)
    {
        fprintf(stderr, "Error: Failed to load TPM public key for host %s\n", host->name);
        return;
    }

    // === Internal Key ===
    EVP_PKEY *internal_key = generate_rsa_key(2048);
    if (!internal_key)
    {
        fprintf(stderr, "Failed to generate internal RSA key\n");
        EVP_PKEY_free(tpm_pubkey);
        return;
    }

    host->spines_internal_public_key = get_public_key(internal_key);
    char *internal_private_pem = get_private_key(internal_key);

    struct HybridEncrypted internal_enc = hybrid_encrypt(
        (unsigned char *)internal_private_pem,
        strlen(internal_private_pem),
        tpm_pubkey);

    host->encrypted_spines_internal_private_key = hybrid_pack(&internal_enc);

    // Cleanup
    free(internal_enc.ciphertext_hex);
    free(internal_enc.enc_key_hex);
    free(internal_private_pem);
    free_rsa_key(internal_key);

    // === External Key ===
    EVP_PKEY *external_key = generate_rsa_key(2048);
    if (!external_key)
    {
        fprintf(stderr, "Failed to generate external RSA key\n");
        EVP_PKEY_free(tpm_pubkey);
        return;
    }

    host->spines_external_public_key = get_public_key(external_key);
    char *external_private_pem = get_private_key(external_key);

    struct HybridEncrypted external_enc = hybrid_encrypt(
        (unsigned char *)external_private_pem,
        strlen(external_private_pem),
        tpm_pubkey);

    host->encrypted_spines_external_private_key = hybrid_pack(&external_enc);

    // Cleanup
    free(external_enc.ciphertext_hex);
    free(external_enc.enc_key_hex);
    free(external_private_pem);
    free_rsa_key(external_key);

    EVP_PKEY_free(tpm_pubkey);
}

/**
 * Generates and encrypts keys and threshold shares for a replica.
 *
 * Creates a 2048-bit RSA key pair for the replica instance and hybrid-encrypts
 * the private key using the host's TPM public key. Also reads the replica's
 * Prime and Scada Master threshold key shares from disk and encrypts them.
 *
 * The encrypted keys and public key are stored in the replica struct.
 *
 * @param replica Pointer to the replica struct to populate.
 * @param host Pointer to the host struct containing the TPM public key.
 * @param site_index Zero-based index of the site this replica belongs to.
 * @param replica_index_within_site Index of the replica within the site (used to locate its share file).
 *
 * @note Exits early and logs errors if key generation, public key loading, or share file reading fails.
 */
void generate_keys_for_replica(struct replica *replica, struct host *host, unsigned site_index, unsigned replica_index_within_site)
{
    if (!host->permanent_public_key)
    {
        fprintf(stderr, "Error: TPM public key missing for host %s (replica %d)\n", host->name, replica->instance_id);
        return;
    }

    // Load the host's public key from PEM
    EVP_PKEY *tpm_pubkey = load_public_key_from_pem(host->permanent_public_key);
    if (!tpm_pubkey)
    {
        fprintf(stderr, "Error: Failed to load TPM public key for host %s (replica %d)\n", host->name, replica->instance_id);
        return;
    }

    // Generate instance key pair
    EVP_PKEY *instance_key = generate_rsa_key(2048);
    if (!instance_key)
    {
        fprintf(stderr, "Error: Failed to generate RSA key for replica %d\n", replica->instance_id);
        EVP_PKEY_free(tpm_pubkey);
        return;
    }

    replica->instance_public_key = get_public_key(instance_key);
    char *instance_private_pem = get_private_key(instance_key);

    struct HybridEncrypted inst_enc = hybrid_encrypt(
        (unsigned char *)instance_private_pem,
        strlen(instance_private_pem),
        tpm_pubkey);
    replica->encrypted_instance_private_key = hybrid_pack(&inst_enc);

    free(instance_private_pem);
    free(inst_enc.ciphertext_hex);
    free(inst_enc.enc_key_hex);
    free_rsa_key(instance_key);

    // Encrypt Threshold Shares

    char prime_share_path[512];
    snprintf(prime_share_path, sizeof(prime_share_path), PRIME_TC_DIR "share%d_%u.pem", replica_index_within_site, site_index + 1);

    char sm_share_path[512];
    snprintf(sm_share_path, sizeof(sm_share_path), SM_TC_DIR "share%d_%u.pem", replica_index_within_site, site_index + 1);

    char *prime_plain = read_file_as_string(prime_share_path);
    char *sm_plain = read_file_as_string(sm_share_path);

    if (!prime_plain || !sm_plain)
    {
        fprintf(stderr, "Error: Failed to read threshold shares for replica %d\n", replica->instance_id);
        free(prime_plain);
        free(sm_plain);
        EVP_PKEY_free(tpm_pubkey);
        return;
    }

    struct HybridEncrypted prime_enc = hybrid_encrypt(
        (unsigned char *)prime_plain,
        strlen(prime_plain),
        tpm_pubkey);
    replica->encrypted_prime_threshold_key_share = hybrid_pack(&prime_enc);

    struct HybridEncrypted sm_enc = hybrid_encrypt(
        (unsigned char *)sm_plain,
        strlen(sm_plain),
        tpm_pubkey);
    replica->encrypted_sm_threshold_key_share = hybrid_pack(&sm_enc);

    // Cleanup
    free(prime_plain);
    free(sm_plain);

    free(prime_enc.ciphertext_hex);
    free(prime_enc.enc_key_hex);

    free(sm_enc.ciphertext_hex);
    free(sm_enc.enc_key_hex);

    EVP_PKEY_free(tpm_pubkey);
}

/**
 * Generates an RSA key pair for a client and encrypts the private key using the host's TPM public key.
 *
 * @param client Pointer to the client struct to populate with keys.
 * @param host Pointer to the host struct containing the TPM public key.
 */
void generate_keys_for_client(struct client *client, struct host *host)
{
    if (!host || !host->permanent_public_key)
    {
        fprintf(stderr, "Error: TPM public key missing for host %s (client %u)\n", host ? host->name : "NULL", client->client_id);
        return;
    }

    EVP_PKEY *tpm_pubkey = load_public_key_from_pem(host->permanent_public_key);
    if (!tpm_pubkey)
    {
        fprintf(stderr, "Error: Failed to load TPM public key for host %s (client %u)\n", host->name, client->client_id);
        return;
    }

    EVP_PKEY *client_key = generate_rsa_key(2048);
    if (!client_key)
    {
        fprintf(stderr, "Error: Failed to generate RSA key for client %u\n", client->client_id);
        EVP_PKEY_free(tpm_pubkey);
        return;
    }

    client->instance_public_key = get_public_key(client_key);
    char *client_priv_pem = get_private_key(client_key);

    struct HybridEncrypted enc = hybrid_encrypt(
        (unsigned char *)client_priv_pem,
        strlen(client_priv_pem),
        tpm_pubkey);

    client->encrypted_instance_private_key = hybrid_pack(&enc);

    // Cleanup
    free(client_priv_pem);
    free(enc.ciphertext_hex);
    free(enc.enc_key_hex);
    free_rsa_key(client_key);
    EVP_PKEY_free(tpm_pubkey);
}

/**
 * Generates simulated TPM key pairs for all hosts in the configuration.
 *
 * Iterates through all sites and hosts in the config, generating a simulated
 * 3072-bit TPM RSA key pair for each host and storing the public key and key path.
 *
 * @param cfg Pointer to the parsed configuration structure containing sites and hosts.
 *
 * @note This should be run before generating or encrypting any other keys that
 * depend on the TPM public key. Part of the first pass.
 */
// void first_pass_generate_tpm_keys(struct config *cfg)
// {
//     for (unsigned i = 0; i < cfg->sites_count; i++)
//     {
//         struct site *site = &cfg->sites[i];

//         for (unsigned j = 0; j < site->hosts_count; j++)
//         {
//             generate_simulated_tpm_key_for_host(&site->hosts[j]);
//         }
//     }
// }

/**
 * Generates and encrypts all internal, external, and replica-specific keys.
 *
 * For each host in the config, generates internal and external RSA key pairs and
 * encrypts them with the host's TPM public key. For each replica, generates an RSA
 * key pair and encrypts associated threshold shares using its host's TPM key.
 *
 * @param cfg Pointer to the configuration structure containing all sites, hosts, and replicas.
 *
 * @note Assumes that TPM keys have already been generated and assigned in a prior pass.
 *       Logs an error if a replica's assigned host cannot be found.
 */
void generate_keys(struct config *cfg)
{
    for (unsigned i = 0; i < cfg->sites_count; i++)
    {
        struct site *site = &cfg->sites[i];

        // Handle hosts
        for (unsigned j = 0; j < site->hosts_count; j++)
        {
            generate_keys_for_host(&site->hosts[j]);
        }

        // Handle replicas
        for (unsigned j = 0; j < site->replicas_count; j++)
        {
            struct replica *replica = &site->replicas[j];
            struct host *replica_host = find_host_for_replica(site, replica->host);

            if (replica_host)
            {
                generate_keys_for_replica(replica, replica_host, i, j);
            }
            else
            {
                fprintf(stderr, "Error: Replica %d has no matching host %s!\n", replica->instance_id, replica->host);
            }
        }

        // Handle clients
        for (unsigned j = 0; j < site->clients_count; j++)
        {
            struct client *client = &site->clients[j];
            struct host *client_host = find_host_for_replica(site, client->host);

            if (client_host)
            {
                generate_keys_for_client(client, client_host);
            }
            else
            {
                fprintf(stderr, "Error: Client %u references unknown host %s\n", client->client_id, client->host);
            }
        }
    }
}

/**
 * Loads the raw YAML configuration processes it, performing all key generation and processing steps.
 *
 * This function orchestrates the full cryptographic setup:
 * 1. Loads the configuration from a YAML file.
 * 2. Generates simulated TPM keys for all hosts.
 * 3. Generates threshold cryptography key shares for all sites.
 * 4. Loads shared threshold public keys into the config.
 * 5. Generates and encrypts internal, external, and replica-specific keys.
 *
 * @param input_yaml Path to the YAML configuration file.
 * @return Pointer to the fully initialized and populated config, or NULL on failure.
 */
struct config *load_and_process_config(const char *input_yaml, int simulate_tpm)
{
    struct config *cfg = load_yaml_config(input_yaml);
    if (!cfg)
        return NULL;

    for (unsigned i = 0; i < cfg->sites_count; i++)
    {
        struct site *site = &cfg->sites[i];
        for (unsigned j = 0; j < site->hosts_count; j++)
        {
            struct host *host = &site->hosts[j];
            EVP_PKEY *priv = load_key_from_file(host->permanent_key_location, 1);
            if (!priv) {
                fprintf(stderr, "Error: Failed to load TPM private key from %s for host %s\n",
                        host->permanent_key_location, host->name);
                free_yaml_config(&cfg);
                return NULL;
            }
            
            char *pub_str = get_public_key(priv);
            EVP_PKEY_free(priv);
            
            if (!pub_str) {
                fprintf(stderr, "Error: Failed to extract TPM public key from %s for host %s\n",
                        host->permanent_key_location, host->name);
                free_yaml_config(&cfg);
                return NULL;
            }
            
            host->permanent_public_key = pub_str;            
        }
    }

    int faults = cfg->tolerated_byzantine_faults;
    int rej_servers = cfg->tolerated_unavailable_replicas;
    int req_shares = faults + 1;
    generate_all_site_tc_keys(req_shares, faults, rej_servers, cfg->sites_count);

    load_threshold_pubkeys(cfg);
    generate_keys(cfg);

    return cfg;
}

/**
 * Loads the Config Manager's RSA key pair from disk.
 *
 * Reads the private and public keys from predefined PEM files in the `cm_keys/` directory
 * and stores them in the provided pointers.
 *
 * @param priv_key Output pointer for the loaded private key.
 * @param pub_key Output pointer for the loaded public key.
 * @return 0 on success, -1 on failure.
 */
int load_config_manager_keys(EVP_PKEY **priv_key, EVP_PKEY **pub_key)
{
    *priv_key = load_key_from_file("cm_keys/private_key.pem", 1);
    *pub_key = load_key_from_file("cm_keys/public_key.pem", 0);
    return (*priv_key && *pub_key) ? 0 : -1;
}
static void Print_Usage(void)
{
    fprintf(stderr,
            "Usage:\n"
            "  ./config_generator <simulate_tpm: 0|1> <input_yaml> <output_yaml>\n\n"
            "Arguments:\n"
            "  simulate_tpm   1 to simulate/generate TPM keys now, 0 to load existing TPM keys\n"
            "  input_yaml     Path to input YAML config file\n"
            "  output_yaml    Path to output signed config file\n");
    exit(EXIT_FAILURE);
}

static void Usage(int argc, char **argv, int *simulate_tpm, const char **input_yaml, const char **output_yaml)
{
    if (argc != 4)
        Print_Usage();

    if (strcmp(argv[1], "1") == 0)
    {
        *simulate_tpm = 1;
    }
    else if (strcmp(argv[1], "0") == 0)
    {
        *simulate_tpm = 0;
    }
    else
    {
        fprintf(stderr, "Invalid value for simulate_tpm: must be 0 or 1\n\n");
        Print_Usage();
    }

    *input_yaml = argv[2];
    *output_yaml = argv[3];
}

int main(int argc, char *argv[])
{
    int simulate_tpm = 0;
    const char *input_path = NULL;
    const char *output_path = NULL;

    Usage(argc, argv, &simulate_tpm, &input_path, &output_path);
    fprintf(stderr, "[INFO] TPM mode: %s\n",
            simulate_tpm ? "simulate" : "use real");

    struct config *cfg = load_and_process_config(input_path, simulate_tpm);
    if (!cfg)
    {
        fprintf(stderr, "Failed to load/process config\n");
        return EXIT_FAILURE;
    }

    EVP_PKEY *cm_priv = NULL, *cm_pub = NULL;
    char *serialized_config = NULL;
    Signature sig = {0};
    FILE *out_fp = NULL;
    int status = EXIT_SUCCESS;

    // Load and process YAML config
    cfg = load_and_process_config(input_path, simulate_tpm);
    if (!cfg)
    {
        fprintf(stderr, "Failed to load or process config\n");
        status = EXIT_FAILURE;
        goto out;
    }

    // Load CM keys
    if (load_config_manager_keys(&cm_priv, &cm_pub) < 0)
    {
        fprintf(stderr, "Failed to load config manager keys\n");
        status = EXIT_FAILURE;
        goto out;
    }

    // Serialize YAML
    size_t serialized_config_len = 0;
    serialized_config = serialize_yaml_config_to_string(cfg, &serialized_config_len);
    if (!serialized_config)
    {
        fprintf(stderr, "Failed to serialize config\n");
        status = EXIT_FAILURE;
        goto out;
    }

    // Sign serialized config
    sig = sign_buffer((unsigned char *)serialized_config, serialized_config_len, cm_priv);
    if (!sig.signature)
    {
        fprintf(stderr, "Failed to sign configuration\n");
        status = EXIT_FAILURE;
        goto out;
    }

    // Write output file
    out_fp = fopen(output_path, "wb");
    if (!out_fp)
    {
        perror("Failed to open output file");
        status = EXIT_FAILURE;
        goto out;
    }

    uint32_t sig_len_u32 = (uint32_t)sig.length;
    fwrite(&sig_len_u32, sizeof(uint32_t), 1, out_fp);
    fwrite(sig.signature, 1, sig.length, out_fp);
    fwrite(serialized_config, 1, serialized_config_len, out_fp);

out:
    if (out_fp)
        fclose(out_fp);
    if (cfg)
        free_yaml_config(&cfg);
    if (serialized_config)
        free(serialized_config);
    free_signature(&sig);
    if (cm_priv)
        EVP_PKEY_free(cm_priv);
    if (cm_pub)
        EVP_PKEY_free(cm_pub);

    return status;
}