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

#include "spu_alarm.h"
#include "spu_events.h"
#include "net_wrapper.h"
#include "spines_lib.h"
#include "parser.h"
#include "key_generation.h"

#define MAX_FRAGMENT_SIZE (MAX_SPINES_CLIENT_MSG - 12)
#define MAX_TOTAL_SIZE (10 * 1024 * 1024) // 10 MB max config
#define CONF_SPINES_MCAST_ADDR "224.0.0.1"
#define CONF_SPINES_MCAST_PORT 8100

typedef struct dummy_conf_fragment
{
    int32u conf_id;
    int32u total_fragments;
    int32u fragment_index;
} conf_fragment;

static int Ctrl_Spines = -1;
static int32u Conf_ID = 1;

static char **fragment_data = NULL;
static size_t *fragment_lens = NULL;
static int received_fragments = 0;
static int expected_fragments = -1;

static void Init_Network(void);
static void Handle_Conf_Message(int s, int source, void *dummy);
static void Assemble_Config(void);

int main(int argc, char **argv)
{
    Alarm_set_types(PRINT | DEBUG);

    Init_Network();

    E_init();
    E_attach_fd(Ctrl_Spines, READ_FD, Handle_Conf_Message, NULL, NULL, HIGH_PRIORITY);
    E_handle_events();
    return 0;
}

static void Init_Network(void)
{
    struct sockaddr_in name;
    struct ip_mreq mreq;

    Ctrl_Spines = Spines_Sock("", CONF_SPINES_MCAST_PORT, SPINES_PRIORITY, 0);
    if (Ctrl_Spines < 0)
    {
        Alarm(EXIT, "Receiver: Error setting up Spines socket\n");
    }

    mreq.imr_multiaddr.s_addr = inet_addr(CONF_SPINES_MCAST_ADDR);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    if (spines_setsockopt(Ctrl_Spines, IPPROTO_IP, SPINES_ADD_MEMBERSHIP, (void *)&mreq, sizeof(mreq)) < 0)
    {
        Alarm(EXIT, "Receiver: Failed to join multicast group\n");
    }

    Alarm(PRINT, "Receiver: Spines multicast network ready\n");
}

static void Handle_Conf_Message(int s, int source, void *dummy) {
    char buffer[MAX_FRAGMENT_SIZE + sizeof(conf_fragment)];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    int ret = spines_recvfrom(s, buffer, sizeof(buffer), 0, (struct sockaddr *)&from_addr, &from_len);
    if (ret <= 0) return;

    if (ret < sizeof(conf_fragment)) {
        Alarm(DEBUG, "Receiver: Received fragment too small\n");
        return;
    }

    conf_fragment *hdr = (conf_fragment *)buffer;
    char *payload = buffer + sizeof(conf_fragment);
    size_t payload_len = ret - sizeof(conf_fragment);

    if (expected_fragments == -1) {
        expected_fragments = hdr->total_fragments;
        Conf_ID = hdr->conf_id;
        Alarm(DEBUG, "Receiver: Expecting %d fragments for conf ID %u\n", expected_fragments, Conf_ID);
        fragment_data = calloc(expected_fragments, sizeof(char *));
        fragment_lens = calloc(expected_fragments, sizeof(size_t));
    }

    if (hdr->conf_id != Conf_ID || hdr->fragment_index >= expected_fragments) {
        Alarm(DEBUG, "Receiver: Unexpected conf_id or fragment index\n");
        return;
    }

    if (fragment_data[hdr->fragment_index] != NULL) {
        Alarm(DEBUG, "Receiver: Duplicate fragment %d ignored\n", hdr->fragment_index);
        return;
    }

    fragment_data[hdr->fragment_index] = malloc(payload_len);
    memcpy(fragment_data[hdr->fragment_index], payload, payload_len);
    fragment_lens[hdr->fragment_index] = payload_len;
    received_fragments++;

    Alarm(DEBUG, "Receiver: Got fragment %d/%d (len=%lu)\n", hdr->fragment_index + 1, expected_fragments, payload_len);

    if (received_fragments == expected_fragments) {
        Alarm(PRINT, "Receiver: All %d fragments received. Assembling config...\n", expected_fragments);
        Assemble_Config();
    }
}

static void Assemble_Config(void)
{
    size_t total_len = 0;
    for (int i = 0; i < expected_fragments; i++) {
        total_len += fragment_lens[i];
    }

    char *assembled = malloc(total_len + 1);
    size_t offset = 0;
    for (int i = 0; i < expected_fragments; i++) {
        memcpy(assembled + offset, fragment_data[i], fragment_lens[i]);
        offset += fragment_lens[i];
        free(fragment_data[i]);
    }
    assembled[total_len] = '\0';
    free(fragment_data);
    free(fragment_lens);

    Alarm(PRINT, "Receiver: Assembled config size = %lu bytes\n", total_len);

    // Parse signature length
    if (total_len < sizeof(uint32_t)) {
        Alarm(PRINT, "Receiver: Total size too small to contain signature length\n");
        free(assembled);
        return;
    }

    uint32_t sig_len = 0;
    memcpy(&sig_len, assembled, sizeof(uint32_t));
    if (total_len < sizeof(uint32_t) + sig_len * 2) {
        Alarm(PRINT, "Receiver: Signature length invalid or incomplete message\n");
        free(assembled);
        return;
    }

    // Decode hex signature
    char *hex_sig = assembled + sizeof(uint32_t);
    char *yaml_data = hex_sig + (sig_len * 2);
    size_t yaml_len = total_len - sizeof(uint32_t) - (sig_len * 2);

    unsigned char *raw_sig = hex_decode(hex_sig, sig_len * 2);
    if (!raw_sig) {
        Alarm(PRINT, "Receiver: Failed to decode hex signature\n");
        free(assembled);
        return;
    }

    EVP_PKEY *cm_pub_key = load_key_from_file("cm_keys/public_key.pem", 0);

    // Verify signature 
    int valid = verify_buffer((unsigned char *)yaml_data, yaml_len, raw_sig, sig_len, cm_pub_key);
    Alarm(PRINT, "Receiver: Signature is %s\n", valid == 0 ? "VALID" : "INVALID");

    // // Parse YAML into config
    // struct config *cfg = load_yaml_config_from_string(yaml_data, yaml_len);
    // if (!cfg) {
    //     Alarm(PRINT, "Receiver: Failed to parse YAML\n");
    //     free(raw_sig);
    //     free(assembled);
    //     return;
    // }

    // Cleanup 
    free(raw_sig);
    free(assembled);
    // free_yaml_config(&cfg);
}
