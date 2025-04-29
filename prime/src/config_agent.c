#define _GNU_SOURCE
#define __USE_MISC

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <time.h>
#include <ifaddrs.h>  

#include "spu_alarm.h"
#include "spu_events.h"
#include "net_wrapper.h"
#include "spines_lib.h"
#include "parser.h"
#include "key_generation.h"
#include "def.h"


#define MAX_DAEMONS 256
#define BASE_SPINES_CONFIG "base_spines.conf"
#define SPINES_INT_FILE "generated_spines_confs/spines_int.conf"
#define SPINES_EXT_FILE "generated_spines_confs/spines_ext.conf"
#define DEFAULT_SPINES_ADDR "127.0.0.1"
#define DEFAULT_SPINES_PORT 8100

typedef struct
{
    const char *ip;
    unsigned id;
} DaemonEntry;

// Fragmentation and message size constants
#define MAX_FRAGMENT_SIZE (MAX_SPINES_CLIENT_MSG - 12)
#define MAX_TOTAL_SIZE (10 * 1024 * 1024) // 10 MB max config

// Structure for each configuration message fragment header
typedef struct dummy_conf_fragment
{
    int32u conf_id;
    int32u total_fragments;
    int32u fragment_index;
} conf_fragment;

// Global state variables
static int Ctrl_Spines = -1;
static int32u Conf_ID = 1;

// Buffers for fragment data and lengths
static char **fragment_data = NULL;
static size_t *fragment_lens = NULL;

// Fragment tracking
static int received_fragments = 0;
static int expected_fragments = -1;

static int32u Last_Seen_Conf_ID = 0;

static char Spines_Addr[32] = DEFAULT_SPINES_ADDR;
static int Spines_Port = DEFAULT_SPINES_PORT;

static void Init_Network(void);
static void Handle_Conf_Message(int s, int source, void *dummy);
static void Usage(int argc, char **argv);
static void Print_Usage(void);

int Assemble_Config_Buffer(char **out_buf, size_t *out_len);
int Verify_Config_Signature(const char *buf, size_t len);
int Handle_Verified_Config(const char *yaml_data, size_t yaml_len);
void Cleanup_Fragments(void);

void generate_spines_topologies(const struct config *cfg);
char *get_my_ip(void);
void decrypt_private_keys(struct config *cfg, const char *my_ip);


int main(int argc, char **argv)
{
    Alarm_set_types(PRINT | DEBUG);

    Usage(argc, argv);

    // set up multicast socket for receiving config messages
    Init_Network();

    // Initialize Spines events
    E_init();

    // attach a handler to the Spines socket for READ events
    E_attach_fd(Ctrl_Spines, READ_FD, Handle_Conf_Message, NULL, NULL, HIGH_PRIORITY);

    // Start the event loop
    E_handle_events();
    return 0;
}

static void Init_Network(void)
{
    struct ip_mreq mreq;

    // create a spines socket to receive the messsages
    Ctrl_Spines = Spines_Sock(Spines_Addr, Spines_Port, SPINES_PRIORITY, CONF_SPINES_MCAST_PORT);
    if (Ctrl_Spines < 0)
    {
        Alarm(EXIT, "Config_Agent: Error setting up Spines socket\n");
    }

    // join the multicast group on any interface
    mreq.imr_multiaddr.s_addr = inet_addr(CONF_SPINES_MCAST_ADDR);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    // add multicast group membership for spines socket
    if (spines_setsockopt(Ctrl_Spines, IPPROTO_IP, SPINES_ADD_MEMBERSHIP, (void *)&mreq, sizeof(mreq)) < 0)
    {
        Alarm(EXIT, "Config_Agent: Failed to join multicast group\n");
    }

    Alarm(PRINT, "Config_Agent: Spines multicast network ready\n");
}

static void Handle_Conf_Message(int s, int source, void *dummy)
{
    char buffer[MAX_FRAGMENT_SIZE + sizeof(conf_fragment)];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    // receive frag from spines socket
    int ret = spines_recvfrom(s, buffer, sizeof(buffer), 0, (struct sockaddr *)&from_addr, &from_len);
    if (ret <= 0)
        return;

    // ignore if fragments too small
    if (ret < sizeof(conf_fragment))
    {
        Alarm(DEBUG, "Config_Agent: Received fragment too small\n");
        return;
    }

    conf_fragment *hdr = (conf_fragment *)buffer;
    char *payload = buffer + sizeof(conf_fragment);
    size_t payload_len = ret - sizeof(conf_fragment);

    // ignore duplicate and older config ids
    if (hdr->conf_id <= Last_Seen_Conf_ID)
    {
        Alarm(DEBUG, "Config_Agent: Ignoring duplicate or older conf_id %u (last seen: %u)\n", hdr->conf_id, Last_Seen_Conf_ID);
        return;
    }

    // if its the first time seeing this config id, or its a new config id
    if (expected_fragments == -1 || hdr->conf_id != Conf_ID)
    {
        // clean up if there are existing fragments
        if (fragment_data != NULL)
        {
            for (int i = 0; i < expected_fragments; i++)
            {
                free(fragment_data[i]);
            }
            free(fragment_data);
            free(fragment_lens);
            fragment_data = NULL;
            fragment_lens = NULL;
        }

        // update state for new config
        expected_fragments = hdr->total_fragments;
        received_fragments = 0;
        Conf_ID = hdr->conf_id;

        fragment_data = calloc(expected_fragments, sizeof(char *));
        fragment_lens = calloc(expected_fragments, sizeof(size_t));

        Alarm(DEBUG, "Config_Agent: Resetting to expect %d fragments for new conf ID %u\n", expected_fragments, Conf_ID);
    }

    // drop unexpected fragments
    if (hdr->conf_id < Conf_ID || hdr->fragment_index >= expected_fragments)
    {
        Alarm(DEBUG, "Config_Agent: Unexpected conf_id or fragment index\n");
        return;
    }

    // drop duplicate f ragments
    if (fragment_data[hdr->fragment_index] != NULL)
    {
        Alarm(DEBUG, "Config_Agent: Duplicate fragment %d ignored\n", hdr->fragment_index);
        return;
    }

    // store fragment
    fragment_data[hdr->fragment_index] = malloc(payload_len);
    memcpy(fragment_data[hdr->fragment_index], payload, payload_len);
    fragment_lens[hdr->fragment_index] = payload_len;
    received_fragments++;

    Alarm(DEBUG, "Config_Agent: Got fragment %d/%d (len=%lu)\n", hdr->fragment_index + 1, expected_fragments, payload_len);

    // if all fragments received, assemble and process the config
    if (received_fragments == expected_fragments)
    {
        Alarm(PRINT, "Config_Agent: All %d fragments received. Assembling config...\n", expected_fragments);

        char *assembled = NULL;
        size_t assembled_len = 0;

        // combine frags into a full buffer
        if (Assemble_Config_Buffer(&assembled, &assembled_len) != 0)
        {
            Alarm(PRINT, "Config_Agent: Failed to assemble config buffer\n");
            Cleanup_Fragments();
            return;
        }

        // verify the signature
        if (Verify_Config_Signature(assembled, assembled_len) != 0)
        {
            Alarm(PRINT, "Config_Agent: Signature is INVALID\n");
            free(assembled);
            Conf_ID = 0;
            Cleanup_Fragments();
            return;
        }

        // parse the yaml and process the config
        if (Handle_Verified_Config(assembled, assembled_len) != 0)
        {
            Alarm(PRINT, "Config_Agent: Failed to handle verified config\n");
        }
        else
        {
            Last_Seen_Conf_ID = Conf_ID;
            Conf_ID = 0;
        }

        free(assembled);
        Cleanup_Fragments();
    }
}

// reassembles fragments into one buffer
int Assemble_Config_Buffer(char **out_buf, size_t *out_len)
{
    if (!fragment_data || !fragment_lens || expected_fragments <= 0)
        return -1;

    size_t total_len = 0;
    for (int i = 0; i < expected_fragments; i++)
    {
        if (!fragment_data[i])
            return -2;
        total_len += fragment_lens[i];
    }

    char *assembled = malloc(total_len);
    if (!assembled)
        return -3;

    size_t offset = 0;
    for (int i = 0; i < expected_fragments; i++)
    {
        memcpy(assembled + offset, fragment_data[i], fragment_lens[i]);
        offset += fragment_lens[i];
    }

    *out_buf = assembled;
    *out_len = total_len;
    return 0;
}

// validates signature on config buffer
int Verify_Config_Signature(const char *buf, size_t len)
{
    if (len < sizeof(uint32_t))
        return -1;

    uint32_t sig_len;
    memcpy(&sig_len, buf, sizeof(uint32_t));
    if (len < sizeof(uint32_t) + sig_len)
        return -2;

    unsigned char *signature = (unsigned char *)(buf + sizeof(uint32_t));
    const char *yaml_data = buf + sizeof(uint32_t) + sig_len;
    size_t yaml_len = len - (sizeof(uint32_t) + sig_len);

    EVP_PKEY *pubkey = load_key_from_file("cm_keys/public_key.pem", 0);
    if (!pubkey)
        return -3;

    int valid = verify_buffer((unsigned char *)yaml_data, yaml_len, signature, sig_len, pubkey);
    EVP_PKEY_free(pubkey);

    return valid == 0 ? 0 : -4;
}

// loads, saves, and processes a verified yaml config
int Handle_Verified_Config(const char *buf, size_t len)
{
    uint32_t sig_len;
    memcpy(&sig_len, buf, sizeof(uint32_t));
    const char *yaml_data = buf + sizeof(uint32_t) + sig_len;
    size_t yaml_len = len - (sizeof(uint32_t) + sig_len);

    struct config *cfg = load_yaml_config_from_string(yaml_data, yaml_len);
    if (!cfg)
        return -1;

    generate_spines_topologies(cfg);
    char *my_ip = get_my_ip();
    decrypt_private_keys(cfg, my_ip);
    free(my_ip);


    const char *dir = "received_configs";
    struct stat st = {0};
    if (stat(dir, &st) == -1)
    {
        mkdir(dir, 0755);
    }

    // write the file out, include timestamp
    // config_<config id>_<timestamp>.yaml
    char filename[512];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    snprintf(filename, sizeof(filename),
             "%s/config_%u_%04d%02d%02d_%02d%02d%02d.yaml",
             dir,
             Conf_ID,
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);

    FILE *fp = fopen(filename, "w");
    if (!fp)
    {
        Alarm(PRINT, "Config_Agent: Failed to write config file to %s\n", filename);
    }
    else
    {
        fwrite(yaml_data, 1, yaml_len, fp);
        fclose(fp);
        Alarm(PRINT, "Config_Agent: Saved config to %s\n", filename);
    }

    free_yaml_config(&cfg);
    return 0;
}

void Cleanup_Fragments(void)
{
    if (fragment_data)
    {
        for (int i = 0; i < expected_fragments; i++)
        {
            free(fragment_data[i]);
        }
        free(fragment_data);
        fragment_data = NULL;
    }

    free(fragment_lens);
    fragment_lens = NULL;

    received_fragments = 0;
    expected_fragments = -1;
}

static void Usage(int argc, char **argv)
{
    int ret;

    while (--argc > 0)
    {
        argv++;
        if ((argc > 1) && (!strncmp(*argv, "-a", 2)))
        {
            ret = snprintf(Spines_Addr, sizeof(Spines_Addr), "%s", argv[1]);
            if (ret < 0 || ret >= sizeof(Spines_Addr))
            {
                Alarm(PRINT, "Invalid Spines IP address: %s\n", argv[1]);
                Print_Usage();
            }
            argc--;
            argv++;
        }
        else if ((argc > 1) && (!strncmp(*argv, "-p", 2)))
        {
            ret = sscanf(argv[1], "%d", &Spines_Port);
            if (ret != 1)
            {
                Alarm(PRINT, "Invalid Spines port: %s\n", argv[1]);
                Print_Usage();
            }
            argc--;
            argv++;
        }
        else
        {
            Print_Usage();
        }
    }
}

static void Print_Usage(void)
{
    Alarm(EXIT, "Usage: ./config_agent\n"
                "    [-a spines_addr] : IP address of Spines daemon to connect to. Default: %s\n"
                "    [-p spines_port] : Port for Spines configuration network. Default: %d\n",
          DEFAULT_SPINES_ADDR, DEFAULT_SPINES_PORT);
}

// determines if an ip is already in a DaemonEntry list
static int ip_in_list(const char *ip, DaemonEntry *list, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        if (strcmp(list[i].ip, ip) == 0)
            return 1;
    }
    return 0;
}

// Appends a DaemonEntry to a DaemonEntry list
static void append_daemon(DaemonEntry *list, size_t *count, const char *ip)
{
    if (!ip_in_list(ip, list, *count))
    {
        list[*count].ip = ip;
        list[*count].id = (unsigned)(*count + 1);
        (*count)++;
    }
}

static void write_topology_file(const char *output_path, DaemonEntry *hosts, size_t host_count, FILE *base_fp)
{
    FILE *out = fopen(output_path, "w");
    if (!out)
    {
        perror("Failed to open output file");
        return;
    }

    // Copy base config from base_spines.conf to output
    fseek(base_fp, 0, SEEK_SET);
    char line[1024];
    while (fgets(line, sizeof(line), base_fp))
    {
        fputs(line, out);
    }

    // Write Hosts section
    fprintf(out, "\nHosts {\n");
    for (size_t i = 0; i < host_count; i++)
    {
        fprintf(out, "    %u %s\n", hosts[i].id, hosts[i].ip);
    }
    fprintf(out, "}\n\n");

    // Write full mesh Edges section
    fprintf(out, "Edges {\n");
    for (size_t i = 0; i < host_count; i++)
    {
        for (size_t j = i + 1; j < host_count; j++)
        {
            fprintf(out, "    %u %u 100\n", hosts[i].id, hosts[j].id);
        }
    }
    fprintf(out, "}\n");

    fclose(out);
}

/**
 * Generates two Spines topology configuration files (`spines_int.conf` and `spines_ext.conf`)
 * based on the parsed YAML configuration structure. The function creates:
 *
 *   - Internal topology (spines_int.conf): A full mesh of hosts marked with `runs_spines_internal`.
 *   - External topology (spines_ext.conf): A full mesh of replica hosts, with each replica also
 *     connected to all client hosts that run `spines_external` in client-type sites.
 *
 * Each topology file is built by copying from a shared base configuration file (`base_spines.conf`),
 * then appending a `Hosts {}` and `Edges {}` section that defines node IDs and connectivity.
 *
 * Parameters:
 *   cfg - Pointer to the in-memory configuration struct.
 *
 * Output:
 *   Writes two files to the current directory:
 *     - spines_int.conf
 *     - spines_ext.conf
 *     - does not write spines_ctrl.conf
 *
 * Notes:
 *   - Host entries are deduplicated by IP.
 *   - Replica host lookups are performed using `find_host_for_replica`.
 *   - Host and edge IDs start at 1.
 */
void generate_spines_topologies(const struct config *cfg)
{
    // declaring arrays to hold DaemonEntries
    DaemonEntry internal_daemons[MAX_DAEMONS];                              // internal spines daemons
    DaemonEntry external_replicas[MAX_DAEMONS];                             // external daemons running on replica hosts
    DaemonEntry external_clients[MAX_DAEMONS];                              // external daemons running on client hosts
    size_t internal_count = 0, replica_ext_count = 0, client_ext_count = 0; // tracks the size

    // for each site
    for (unsigned i = 0; i < cfg->sites_count; i++)
    {
        struct site *site = &cfg->sites[i];

        // Hosts
        for (unsigned j = 0; j < site->hosts_count; j++)
        {
            struct host *h = &site->hosts[j];

            // Collect hosts that run internal Spines
            if (h->runs_spines_internal)
            {
                append_daemon(internal_daemons, &internal_count, h->ip);
            }

            // If this is a CLIENT site, collect external daemons (like PLC and HMI)
            if (site->type == CLIENT && h->runs_spines_external)
            {
                append_daemon(external_clients, &client_ext_count, h->ip);
            }
        }

        // Replicas
        if (site->type != DATA_CENTER)
        {
            for (unsigned j = 0; j < site->replicas_count; j++)
            {
                struct replica *r = &site->replicas[j];
                struct host *replica_host = find_host_for_replica(site, r->host);

                if (replica_host && replica_host->runs_spines_external)
                {
                    append_daemon(external_replicas, &replica_ext_count, replica_host->ip);
                }
            }
        }
    }

    // write the internal topology
    FILE *base_fp = fopen(BASE_SPINES_CONFIG, "r");
    if (!base_fp)
    {
        perror("Failed to open base config file");
        return;
    }
    write_topology_file(SPINES_INT_FILE, internal_daemons, internal_count, base_fp);

    // write the external topology (replica + client edges)
    FILE *out = fopen(SPINES_EXT_FILE, "w");
    if (!out)
    {
        perror("Failed to open spines_ext.conf");
        fclose(base_fp);
        return;
    }

    fseek(base_fp, 0, SEEK_SET);
    char line[1024];
    while (fgets(line, sizeof(line), base_fp))
    {
        fputs(line, out);
    }
    fclose(base_fp);

    fprintf(out, "\nHosts {\n");
    for (size_t i = 0; i < replica_ext_count; i++)
        fprintf(out, "    %u %s\n", i + 1, external_replicas[i].ip);
    for (size_t i = 0; i < client_ext_count; i++)
        fprintf(out, "    %u %s\n", (unsigned)(replica_ext_count + i + 1), external_clients[i].ip);
    fprintf(out, "}\n\n");

    fprintf(out, "Edges {\n");
    for (size_t i = 0; i < replica_ext_count; i++)
    {
        for (size_t j = i + 1; j < replica_ext_count; j++)
        {
            fprintf(out, "    %u %u 100\n", i + 1, j + 1);
        }
        for (size_t j = 0; j < client_ext_count; j++)
        {
            fprintf(out, "    %u %u 100\n", i + 1, (unsigned)(replica_ext_count + j + 1));
        }
    }
    fprintf(out, "}\n");
    fclose(out);
}


char *get_my_ip()
{
    struct ifaddrs *ifaddr, *ifa;
    char *ip = NULL;

    if (getifaddrs(&ifaddr) == -1)
        return NULL;

    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next)
    {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
            continue;

        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        if (strcmp(ifa->ifa_name, "lo") == 0) // Skip loopback
            continue;

        ip = strdup(inet_ntoa(sa->sin_addr));
        break;
    }

    freeifaddrs(ifaddr);
    return ip;
}

static struct host *find_my_host(struct config *cfg, const char *my_ip)
{
    for (unsigned i = 0; i < cfg->sites_count; i++) {
        struct site *site = &cfg->sites[i];
        for (unsigned j = 0; j < site->hosts_count; j++) {
            struct host *h = &site->hosts[j];
            if (strcmp(h->ip, my_ip) == 0)
                return h;
        }
    }
    return NULL;
}

void decrypt_private_keys(struct config *cfg, const char *my_ip)
{
    struct host *my_host = find_my_host(cfg, my_ip);
    if (!my_host) {
        Alarm(PRINT, "Cannot find host matching IP: %s\n", my_ip);
        return;
    }
    printf("[DEBUG] I am host: %s (IP: %s), TPM key at: %s\n", my_host->name, my_ip, my_host->permanent_key_location);

    EVP_PKEY *tpm_priv = load_key_from_file(my_host->permanent_key_location, 1);
    if (!tpm_priv) {
        Alarm(PRINT, "Failed to load TPM private key for my host\n");
        return;
    }

    // Decrypt my spines internal key
    if (my_host->encrypted_spines_internal_private_key) {
        char *enc_key_hex, *ciphertext_hex;
        hybrid_unpack(my_host->encrypted_spines_internal_private_key, &enc_key_hex, &ciphertext_hex);
        struct HybridDecryptionResult dec = hybrid_decrypt(ciphertext_hex, enc_key_hex, tpm_priv);
        my_host->unencrypted_spines_internal_private_key = dec.plaintext;
        free(enc_key_hex);
        free(ciphertext_hex);
        printf("\n[Internal Spines Private Key]:\n%s\n", dec.plaintext);
    }

    // Decrypt my spines external key
    if (my_host->encrypted_spines_external_private_key) {
        char *enc_key_hex, *ciphertext_hex;
        hybrid_unpack(my_host->encrypted_spines_external_private_key, &enc_key_hex, &ciphertext_hex);
        struct HybridDecryptionResult dec = hybrid_decrypt(ciphertext_hex, enc_key_hex, tpm_priv);
        my_host->unencrypted_spines_external_private_key = dec.plaintext;
        free(enc_key_hex);
        free(ciphertext_hex);
        printf("\n[External Spines Private Key]:\n%s\n", my_host->unencrypted_spines_external_private_key);
    }

    // Decrypt replicas assigned to my_host
    for (unsigned i = 0; i < cfg->sites_count; i++) {
        struct site *site = &cfg->sites[i];
        for (unsigned j = 0; j < site->replicas_count; j++) {
            struct replica *rep = &site->replicas[j];
            struct host *rep_host = find_host_for_replica(site, rep->host);

            if (rep_host == my_host) {
                if (rep->encrypted_instance_private_key) {
                    char *enc_key_hex, *ciphertext_hex;
                    hybrid_unpack(rep->encrypted_instance_private_key, &enc_key_hex, &ciphertext_hex);
                    struct HybridDecryptionResult dec = hybrid_decrypt(ciphertext_hex, enc_key_hex, tpm_priv);
                    rep->unencrypted_instance_private_key = dec.plaintext;
                    free(enc_key_hex);
                    free(ciphertext_hex);
                    printf("\n[Instance Private Key] (Replica %d):\n%s\n", rep->instance_id, rep->unencrypted_instance_private_key);
                }

                if (rep->encrypted_prime_threshold_key_share) {
                    char *enc_key_hex, *ciphertext_hex;
                    hybrid_unpack(rep->encrypted_prime_threshold_key_share, &enc_key_hex, &ciphertext_hex);
                    struct HybridDecryptionResult dec = hybrid_decrypt(ciphertext_hex, enc_key_hex, tpm_priv);
                    rep->unencrypted_prime_threshold_key_share = dec.plaintext;
                    free(enc_key_hex);
                    free(ciphertext_hex);
                    printf("\n[Prime Threshold Share] (Replica %d):\n%s\n", rep->instance_id, rep->unencrypted_prime_threshold_key_share);
                }

                if (rep->encrypted_sm_threshold_key_share) {
                    char *enc_key_hex, *ciphertext_hex;
                    hybrid_unpack(rep->encrypted_sm_threshold_key_share, &enc_key_hex, &ciphertext_hex);
                    struct HybridDecryptionResult dec = hybrid_decrypt(ciphertext_hex, enc_key_hex, tpm_priv);
                    rep->unencrypted_sm_threshold_key_share = dec.plaintext;
                    free(enc_key_hex);
                    free(ciphertext_hex);
                    printf("\n[SM Threshold Share] (Replica %d):\n%s\n", rep->instance_id, rep->unencrypted_sm_threshold_key_share);
                }
            }
        }
    }

    EVP_PKEY_free(tpm_priv);
}
