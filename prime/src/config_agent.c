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

#include "spu_alarm.h"
#include "spu_events.h"
#include "net_wrapper.h"
#include "spines_lib.h"
#include "parser.h"
#include "key_generation.h"
#include "config_manager.h"

#define MAX_DAEMONS 256
#define BASE_SPINES_CONFIG "base_spines.conf"
#define SPINES_INT_FILE "spines_int.conf"
#define SPINES_EXT_FILE "spines_ext.conf"

typedef struct
{
    const char *ip;
    unsigned id;
} DaemonEntry;

// Fragmentation and message size constants
#define MAX_FRAGMENT_SIZE (MAX_SPINES_CLIENT_MSG - 12)
#define MAX_TOTAL_SIZE (10 * 1024 * 1024) // 10 MB max config

// Spines multicast address and port for configuration messages
#define CONF_SPINES_MCAST_ADDR "224.0.0.1"
#define CONF_SPINES_MCAST_PORT 8100

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

static void Init_Network(void);
static void Handle_Conf_Message(int s, int source, void *dummy);
static void Assemble_Config(void);

int main(int argc, char **argv)
{
    Alarm_set_types(PRINT | DEBUG);

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
    struct sockaddr_in name;
    struct ip_mreq mreq;

    // create a spines socket to receive the messsages
    Ctrl_Spines = Spines_Sock("", CONF_SPINES_MCAST_PORT, SPINES_PRIORITY, 0);
    if (Ctrl_Spines < 0)
    {
        Alarm(EXIT, "Receiver: Error setting up Spines socket\n");
    }

    // join t he multicast group on any interface
    mreq.imr_multiaddr.s_addr = inet_addr(CONF_SPINES_MCAST_ADDR);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    // add multicast group membership for spines socket
    if (spines_setsockopt(Ctrl_Spines, IPPROTO_IP, SPINES_ADD_MEMBERSHIP, (void *)&mreq, sizeof(mreq)) < 0)
    {
        Alarm(EXIT, "Receiver: Failed to join multicast group\n");
    }

    Alarm(PRINT, "Receiver: Spines multicast network ready\n");
}

static void Handle_Conf_Message(int s, int source, void *dummy)
{
    // buffer for incoming message
    char buffer[MAX_FRAGMENT_SIZE + sizeof(conf_fragment)];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    // receive the message
    int ret = spines_recvfrom(s, buffer, sizeof(buffer), 0, (struct sockaddr *)&from_addr, &from_len);
    if (ret <= 0)
        return;

    // ensure enough bytes for full header
    if (ret < sizeof(conf_fragment))
    {
        Alarm(DEBUG, "Receiver: Received fragment too small\n");
        return;
    }

    // interpret header and extract the payload and length
    conf_fragment *hdr = (conf_fragment *)buffer;
    char *payload = buffer + sizeof(conf_fragment);
    size_t payload_len = ret - sizeof(conf_fragment);

    // on first fragment initialize buffers and tracking of total frags
    if (expected_fragments == -1)
    {
        expected_fragments = hdr->total_fragments;
        Conf_ID = hdr->conf_id;
        Alarm(DEBUG, "Receiver: Expecting %d fragments for conf ID %u\n", expected_fragments, Conf_ID);
        fragment_data = calloc(expected_fragments, sizeof(char *));
        fragment_lens = calloc(expected_fragments, sizeof(size_t));
    }

    // check for mismatched config ID or out of bounds
    if (hdr->conf_id != Conf_ID || hdr->fragment_index >= expected_fragments)
    {
        Alarm(DEBUG, "Receiver: Unexpected conf_id or fragment index\n");
        return;
    }

    // check for duplicates
    if (fragment_data[hdr->fragment_index] != NULL)
    {
        Alarm(DEBUG, "Receiver: Duplicate fragment %d ignored\n", hdr->fragment_index);
        return;
    }

    // store the fragment data
    fragment_data[hdr->fragment_index] = malloc(payload_len);
    memcpy(fragment_data[hdr->fragment_index], payload, payload_len);
    fragment_lens[hdr->fragment_index] = payload_len;
    received_fragments++;

    Alarm(DEBUG, "Receiver: Got fragment %d/%d (len=%lu)\n", hdr->fragment_index + 1, expected_fragments, payload_len);

    // if we got all the fragments then assemble the config
    if (received_fragments == expected_fragments)
    {
        Alarm(PRINT, "Receiver: All %d fragments received. Assembling config...\n", expected_fragments);
        Assemble_Config();
    }
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

void generate_spines_topologies(const struct config *cfg)
{
    //declaring arrays to hold DaemonEntries
    DaemonEntry internal_daemons[MAX_DAEMONS];
    DaemonEntry external_replicas[MAX_DAEMONS];
    DaemonEntry external_clients[MAX_DAEMONS];
    size_t internal_count = 0, replica_ext_count = 0, client_ext_count = 0;

    // for each site
    for (unsigned i = 0; i < cfg->sites_count; i++)
    {
        struct site *site = &cfg->sites[i];

        // Hosts
        // for each host
        for (unsigned j = 0; j < site->hosts_count; j++)
        {
            // if it runs spines internal, add it to the internal daemon list
            struct host *h = &site->hosts[j];
            if (h->runs_spines_internal)
                append_daemon(internal_daemons, &internal_count, h->ip);
            // if it runs spines external, add it to the external daemon list
            if (h->runs_spines_external && site->type == CLIENT)
                append_daemon(external_clients, &client_ext_count, h->ip);
        }

        // Replicas
        // for each replica
        for (unsigned j = 0; j < site->replicas_count; j++)
        {
            //ensure theres a host and add it to external replica daemons list
            struct replica *r = &site->replicas[j];
            struct host *replica_host = find_host_for_replica(site, r->host);
            if (replica_host && replica_host->ip)
                append_daemon(external_replicas, &replica_ext_count, replica_host->ip);
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

static void Assemble_Config(void)
{
    // Reassemble message
    size_t total_len = 0;
    for (int i = 0; i < expected_fragments; i++)
    {
        total_len += fragment_lens[i];
    }

    char *assembled = malloc(total_len);
    size_t offset = 0;
    for (int i = 0; i < expected_fragments; i++)
    {
        memcpy(assembled + offset, fragment_data[i], fragment_lens[i]);
        offset += fragment_lens[i];
        free(fragment_data[i]);
    }
    free(fragment_data);
    free(fragment_lens);

    // Parse signature length and signature
    if (total_len < sizeof(uint32_t))
    {
        Alarm(PRINT, "Receiver: Message too short\n");
        free(assembled);
        return;
    }

    uint32_t sig_len;
    memcpy(&sig_len, assembled, sizeof(uint32_t));

    if (total_len < sizeof(uint32_t) + sig_len)
    {
        Alarm(PRINT, "Receiver: Signature too short\n");
        free(assembled);
        return;
    }

    unsigned char *signature = (unsigned char *)(assembled + sizeof(uint32_t));
    char *yaml_data = (char *)(assembled + sizeof(uint32_t) + sig_len);
    size_t yaml_len = total_len - (sizeof(uint32_t) + sig_len);

    // Verify the signature using cm_keys/public_key.pem
    EVP_PKEY *pubkey = load_key_from_file("cm_keys/public_key.pem", 0);
    if (!pubkey)
    {
        Alarm(PRINT, "Receiver: Failed to load CM public key\n");
        free(assembled);
        return;
    }

    int valid = verify_buffer((unsigned char *)yaml_data, yaml_len, signature, sig_len, pubkey);
    Alarm(PRINT, "Receiver: Signature is %s\n", valid == 0 ? "VALID" : "INVALID");
    EVP_PKEY_free(pubkey);

    if (valid != 0)
    {
        free(assembled);
        return;
    }

    // Parse YAML into config
    struct config *cfg = load_yaml_config_from_string(yaml_data, yaml_len);
    if (!cfg)
    {
        Alarm(PRINT, "Receiver: Failed to parse YAML config\n");
        free(assembled);
        return;
    }

    Alarm(PRINT, "Receiver: Parsed config ID = %u\n", cfg->configuration_id);

    // Create directory if needed
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
             cfg->configuration_id,
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);

    FILE *fp = fopen(filename, "w");
    if (!fp)
    {
        Alarm(PRINT, "Receiver: Failed to write config file to %s\n", filename);
    }
    else
    {
        fwrite(yaml_data, 1, yaml_len, fp);
        fclose(fp);
        Alarm(PRINT, "Receiver: Saved config to %s\n", filename);
    }

    // decrypt all encrypted private/threshold keys
    // ?? Should all of them be decrypted? or just this hosts? or not at all?
    decrypt_all_private_keys(cfg);

    // by now we should have verified the config, parsed it, and decrypted all encrypted private keys.

    // Clean up
    free_yaml_config(&cfg);
    free(assembled);
}
