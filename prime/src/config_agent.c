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
#include <dirent.h>

#include "spu_alarm.h"
#include "spu_events.h"
#include "net_wrapper.h"
#include "spines_lib.h"
#include "parser.h"
#include "key_generation.h"
#include "def.h"

#define MAX_DAEMONS 256
#define BASE_SPINES_CONFIG "base_spines.conf"
#define SPINES_INT_FILE "../../spines/daemon/spines_int.conf"
#define SPINES_EXT_FILE "../../spines/daemon/spines_ext.conf"
#define DEFAULT_SPINES_ADDR "127.0.0.1"
#define DEFAULT_SPINES_PORT 8200

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
static char latest_config_path[512] = "received_configs/latest.yaml";

static char Spines_Addr[32] = DEFAULT_SPINES_ADDR;
static int Spines_Port = DEFAULT_SPINES_PORT;
static char Host_Name[128] = {0}; // Empty string by default

static void Init_Network(void);
static void Handle_Conf_Message(int s, int source, void *dummy);
static void Usage(int argc, char **argv);
static void Print_Usage(void);

int Assemble_Config_Buffer(char **out_buf, size_t *out_len);
int Verify_Config_Signature(const char *buf, size_t len);
int Handle_Verified_Config(const char *yaml_data, size_t yaml_len);
void Cleanup_Fragments(void);

static struct host *find_host_by_name(struct config *cfg, const char *name);
void start_components_from_config(const struct config *cfg, const struct host *me);
int kill_all_components(void);

void generate_spines_topologies(const struct config *cfg);

int main(int argc, char **argv)
{
    Alarm_set_types(PRINT | DEBUG);

    Usage(argc, argv);

    // set up multicast socket for receiving config messages
    Init_Network();

    // Initialize Spines events
    E_init();

    // attach a handler to the Spines socket for READ events
    E_attach_fd(Ctrl_Spines, READ_FD, Handle_Conf_Message, 0, NULL, HIGH_PRIORITY);

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

/**
 * Reassembles received configuration fragments into a single contiguous buffer.
 *
 * Iterates over all received fragments and concatenates their payloads in order into
 * a newly allocated buffer. The caller receives both the pointer to the assembled
 * buffer and its total length.
 *
 * @param[out] out_buf Pointer to the output buffer (must be freed by the caller).
 * @param[out] out_len Pointer to the size of the assembled buffer.
 *
 * @return 0 on success,
 *        -1 if input state is invalid or uninitialized,
 *        -2 if any fragment is missing,
 *        -3 if memory allocation fails.
 */
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

/**
 * Verifies the signature on a received configuration buffer.
 *
 * The buffer format is expected to be:
 *   [4 bytes signature length][signature][YAML config data]
 *
 * This function extracts the signature and YAML data from the buffer, loads
 * the Config Manager's public key from disk, and verifies the signature.
 *
 * @param buf Pointer to the full configuration buffer.
 * @param len Total length of the buffer.
 *
 * @return  0 if the signature is valid,
 *         -1 if the buffer is too short to contain a signature,
 *         -2 if the buffer is too short to contain the declared signature length,
 *         -3 if the public key could not be loaded,
 *         -4 if the signature verification failed.
 */
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

/**
 * Parses, processes, and saves a verified YAML configuration buffer.
 *
 * Extracts the YAML portion from a signed buffer (after the signature), parses it
 * into a config structure, generates Spines topology files, decrypts private keys
 * for the current host, and saves the YAML to a timestamped file under
 * `received_configs/`.
 *
 * @param buf Pointer to the full signed buffer (signature + YAML config).
 * @param len Length of the full buffer in bytes.
 *
 * @return  0 on success,
 *         -1 if the YAML parsing fails.
 *
 * @note Assumes signature has already been verified. Relies on global `Conf_ID`
 *       to name the saved config file, rather than the configuration_id field in the YAML.
 */
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
    struct host *me = find_host_by_name(cfg, Host_Name);
    if (!me)
    {
        Alarm(EXIT, "Could not find host '%s' in config\n", Host_Name);
    }
    const char *my_ip = me->ip;
    kill_all_components();
    sleep(1);

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

    // Also save a consistent copy as latest.yaml
    FILE *latest_fp = fopen(latest_config_path, "w");
    if (!latest_fp)
    {
        Alarm(PRINT, "Config_Agent: Failed to write latest config to %s\n", latest_config_path);
    }
    else
    {
        fwrite(yaml_data, 1, yaml_len, latest_fp);
        fclose(latest_fp);
        Alarm(PRINT, "Config_Agent: Updated %s with latest config\n", latest_config_path);
    }

    start_components_from_config(cfg, me);

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
    int got_host_name = 0;

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
        else if ((argc > 1) && (!strncmp(*argv, "-h", 2)))
        {
            ret = snprintf(Host_Name, sizeof(Host_Name), "%s", argv[1]);
            if (ret < 0 || ret >= sizeof(Host_Name))
            {
                Alarm(PRINT, "Invalid host name: %s\n", argv[1]);
                Print_Usage();
            }
            got_host_name = 1;
            argc--;
            argv++;
        }
        else
        {
            Print_Usage();
        }
    }

    if (!got_host_name)
    {
        Alarm(PRINT, "Missing required argument: -h host_name\n");
        Print_Usage();
    }
}

static void Print_Usage(void)
{
    Alarm(EXIT, "Usage: ./config_agent -h host_name\n"
                "    [-a spines_addr] : IP address of Spines daemon to connect to. Default: %s\n"
                "    [-p spines_port] : Port for Spines configuration network. Default: %d\n"
                "    -h host_name     : REQUIRED. Host name to match in config.\n",
          DEFAULT_SPINES_ADDR, DEFAULT_SPINES_PORT);
}

/**
 * Checks if an IP address already exists in a list of DaemonEntry structs.
 *
 * Iterates through the list and compares the given IP against each entry.
 *
 * @param ip     The IP address to search for.
 * @param list   Array of DaemonEntry structs.
 * @param count  Number of entries in the list.
 *
 * @return 1 if the IP is found, 0 otherwise.
 */
static int ip_in_list(const char *ip, DaemonEntry *list, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        if (strcmp(list[i].ip, ip) == 0)
            return 1;
    }
    return 0;
}

static struct host *find_host_by_name(struct config *cfg, const char *name)
{
    for (unsigned i = 0; i < cfg->sites_count; i++)
    {
        struct site *site = &cfg->sites[i];
        for (unsigned j = 0; j < site->hosts_count; j++)
        {
            if (strcmp(site->hosts[j].name, name) == 0)
                return &site->hosts[j];
        }
    }
    return NULL;
}

/**
 * Appends a new DaemonEntry to the list if the IP is not already present.
 *
 * Assigns a new ID based on the current count and increments the count.
 *
 * @param list   Array of DaemonEntry structs to append to.
 * @param count  Pointer to the current number of entries (updated on append).
 * @param ip     The IP address to add.
 */
static void append_daemon(DaemonEntry *list, size_t *count, const char *ip)
{
    if (!ip_in_list(ip, list, *count))
    {
        list[*count].ip = ip;
        list[*count].id = (unsigned)(*count + 1);
        (*count)++;
    }
}

/**
 * Writes a Spines topology file with host and edge definitions.
 *
 * Copies the base configuration into the output file, then appends:
 * - A Hosts section with assigned IDs and IPs.
 * - An Edges section forming a full mesh between all hosts.
 *
 * @param output_path Path to the output `.conf` file to write.
 * @param hosts       Array of DaemonEntry structs containing IPs and IDs.
 * @param host_count  Number of host entries in the array.
 * @param base_fp     File pointer to the base configuration template.
 */
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
 * Generates Spines topology configuration files for internal and external communication.
 *
 * This function creates two topology files:
 *   - `spines_int.conf`: A full mesh network of hosts running internal Spines daemons.
 *   - `spines_ext.conf`: A full mesh of replica hosts running external Spines daemons, plus connections
 *                        from each replica to all external client hosts in client-type sites.
 *
 * It deduplicates hosts by IP and assigns each one a unique ID. The topology files are generated
 * by copying a base configuration (`base_spines.conf`) and appending `Hosts {}` and `Edges {}` sections.
 *
 * @param cfg Pointer to the loaded system configuration.
 *
 * @note Output files are:
 *   - `spines_int.conf` written to the current directory.
 *   - `spines_ext.conf` written to the current directory.
 *
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
        fprintf(out, "    %zu %s\n", i + 1, external_replicas[i].ip);
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

/**
 * Checks if a process name matches a known component (spines, prime, or scada_master).
 *
 * Compares the given process name against a predefined list of target component names.
 *
 * @param name Name of the process (as read from /proc/<pid>/comm).
 *
 * @return 1 if the name matches a target process, 0 otherwise.
 */
int is_target_process(const char *name)
{
    const char *targets[] = {"spines", "prime", "scada_master"};
    const int num_targets = sizeof(targets) / sizeof(targets[0]);
    for (int i = 0; i < num_targets; i++)
    {
        if (strcmp(name, targets[i]) == 0)
        {
            return 1;
        }
    }
    return 0;
}

/**
 * Scans all running processes and forcibly terminates known system components.
 *
 * Iterates over all entries in `/proc`, identifies processes whose names match
 * known component names (`spines`, `prime`, `scada_master`), and sends them `SIGKILL`.
 *
 * @return Number of processes successfully killed, or -1 on failure to open /proc.
 */
int kill_all_components()
{
    DIR *proc_dir = opendir("/proc");
    struct dirent *entry;
    int killed = 0;

    pid_t my_pid = getpid();

    if (!proc_dir)
    {
        perror("opendir /proc");
        return -1;
    }

    // iterate through all of proc
    while ((entry = readdir(proc_dir)) != NULL)
    {
        // considering only directories (pids are dirs)
        if (entry->d_type != DT_DIR)
            continue;

        // dir name to pid
        pid_t pid = atoi(entry->d_name);
        if (pid <= 0)
            continue;

        // construct a path
        char comm_path[64];
        snprintf(comm_path, sizeof(comm_path), "/proc/%d/comm", pid);

        // open the file to read the process name
        FILE *comm_file = fopen(comm_path, "r");
        if (!comm_file)
            continue; // couldnt open

        char comm[256];
        if (fgets(comm, sizeof(comm), comm_file))
        {
            // rm newline
            comm[strcspn(comm, "\n")] = 0;

            // if is a target process
            if (is_target_process(comm))
            {
                int skip = 0;

                // Special case: only skip spines if it's running spines_ctrl.conf
                if (strcmp(comm, "spines") == 0)
                {
                    char cmdline_path[64];
                    snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", pid);
                    FILE *cmdline_file = fopen(cmdline_path, "r");
                    if (cmdline_file)
                    {
                        char cmdline[1024];
                        size_t len = fread(cmdline, 1, sizeof(cmdline) - 1, cmdline_file);
                        fclose(cmdline_file);

                        if (len > 0)
                        {
                            cmdline[len] = '\0';

                            // Replace nulls with spaces to log clearly
                            for (size_t i = 0; i < len; i++)
                            {
                                if (cmdline[i] == '\0')
                                    cmdline[i] = ' ';
                            }

                            if (strstr(cmdline, "spines_ctrl.conf") != NULL)
                            {
                                skip = 1;
                            }
                        }
                    }
                }

                if (skip)
                {
                    printf("Skipping %s (PID %d) — spines_ctrl.conf detected\n", comm, pid);
                    continue;
                }

                if (kill(pid, SIGKILL) == 0)
                {
                    printf("Killed %s (PID %d)\n", comm, pid);
                    killed++;
                }
                else
                {
                    perror("kill");
                }
            }
        }

        fclose(comm_file); // close file
    }

    closedir(proc_dir); // close proc
    return killed;      // return number of killed processes
}

void remove_spines_tmp_files() {
    char cmd[512];

    snprintf(cmd, sizeof(cmd),
        "rm -f /tmp/spines%d /tmp/spines%ddata /tmp/spines%d /tmp/spines%ddata",
        SPINES_PORT, SPINES_PORT, SPINES_EXT_PORT, SPINES_EXT_PORT);

    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "Failed to remove spines tmp files with command: %s\n", cmd);
    }
}

// TODO: take in spines internal,external,ctrl port parameter?
void start_components_from_config(const struct config *cfg, const struct host *me)
{
    // if passing port parameters convert this to remove spines<port> tmp files
    remove_spines_tmp_files();

    char cmd[1024];

    Alarm(PRINT, "\n=== Checking for Replicas to Start ===");

    // Check for replicas on this host
    for (unsigned i = 0; i < cfg->sites_count; i++)
    {
        struct site *site = &cfg->sites[i];

        // Start prime and scada_master if replicas match
        for (unsigned j = 0; j < site->replicas_count; j++)
        {
            struct replica *r = &site->replicas[j];
            struct host *rep_host = find_host_for_replica(site, r->host);

            if (rep_host == me)
            {
                // Start internal Spines daemon if this host is configured for it
                if (me->runs_spines_internal)
                {
                    snprintf(cmd, sizeof(cmd),
                             "cd ../../spines/daemon && ./spines -p %d -c spines_int.conf -I %s > ../../prime/bin/logs/spines_int.log &",
                             SPINES_PORT, me->ip);
                    Alarm(PRINT, "\nStarting internal Spines: %s", cmd);
                    system(cmd);
                }

                // Start external Spines daemon if this host is configured for it
                if (me->runs_spines_external)
                {
                    snprintf(cmd, sizeof(cmd),
                             "cd ../../spines/daemon && ./spines -p %d -c spines_ext.conf -I %s > ../../prime/bin/logs/spines_ext.log &",
                             SPINES_EXT_PORT, me->ip);
                    Alarm(PRINT, "\nStarting external Spines: %s", cmd);
                    system(cmd);
                }

                Alarm(PRINT, "\nStarting replica instance %u from site %u", r->instance_id, i);
                // Pass config to scada_master and prime
                snprintf(cmd, sizeof(cmd),
                         "cd ../../scada_master && ./scada_master %u %u > ../prime/bin/logs/sm.log &",
                         r->instance_id, r->instance_id,
                         me->ip, SPINES_PORT, me->ip, SPINES_EXT_PORT);

                Alarm(PRINT, "\nStarting scada_master: %s", cmd);
                system(cmd);

                snprintf(cmd, sizeof(cmd),
                         "./prime -i %u -g %u > logs/prime.log &",
                         r->instance_id, r->instance_id);
                Alarm(PRINT, "\nStarting prime: %s", cmd);
                system(cmd);
            }
        }

        // Start client programs
        if (site->type == CLIENT)
        {
            Alarm(PRINT, "\n=== Checking for Clients to Start (Site %u) ===", i);
            for (unsigned j = 0; j < site->clients_count; j++)
            {
                struct client *c = &site->clients[j];
                struct host *client_host = find_host_for_replica(site, c->host);

                if (client_host == me && c->type)
                {
                    // Client programs have been refactored to default to received_configs/latest.yaml
                    // so no config path argument is necessary but can be passed for each with -c <path>
                    snprintf(cmd, sizeof(cmd),
                             "cd ../../spines/daemon && ./spines -p %d -c spines_ext.conf -I %s > logs/spines_ext.log &",
                             SPINES_EXT_PORT, me->ip);
                    Alarm(PRINT, "\nStarting external Spines (client host): %s", cmd);
                    system(cmd);

                    if (strcmp(c->type, "JHU") == 0)
                    {
                        snprintf(cmd, sizeof(cmd), "cd hmis/ && ./jhu_hmi/jhu_hmi &");
                        Alarm(PRINT, "\nStarting JHU HMI: %s", cmd);
                    }
                    else if (strcmp(c->type, "PNNL") == 0)
                    {
                        snprintf(cmd, sizeof(cmd), "cd hmis/ && ./pnnl_hmi/pnnl_hmi &");
                        Alarm(PRINT, "\nStarting PNNL HMI: %s", cmd);
                    }
                    else if (strcmp(c->type, "EMS") == 0)
                    {
                        snprintf(cmd, sizeof(cmd), "cd hmis/ && ./ems_hmi/ems_hmi &");
                        Alarm(PRINT, "\nStarting EMS HMI: %s", cmd);
                    }
                    else if (strcmp(c->type, "proxy") == 0)
                    {
                        // snprintf(cmd, sizeof(cmd), "cd proxy/ && ./proxy <ID> <Num_RTU_Emulated> &");
                        snprintf(cmd, sizeof(cmd), "cd proxy/ && ./proxy <ID> <Num_RTU_Emulated> &");

                        Alarm(PRINT, "\nStarting Proxy Client: %s", cmd);
                    }
                    else if (strcmp(c->type, "benchmark") == 0)
                    {
                        // snprintf(cmd, sizeof(cmd), "cd benchmark/ && ./benchmark <ID> <Poll_Frequency(usec)> <Num_Polls> &");
                        snprintf(cmd, sizeof(cmd), "cd benchmark/ && ./benchmark <ID> <Poll_Frequency(usec)> <Num_Polls> &");

                        Alarm(PRINT, "\nStarting Benchmark Client: %s", cmd);
                    }
                    else
                    {
                        Alarm(PRINT, "\nUnknown client type '%s' for client %u — skipping", c->type, c->client_id);
                        continue;
                    }
                    system(cmd);
                }
            }
        }
    }
    Alarm(PRINT, "\n=== Component Startup Complete ===\n");
}
