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

static void Handle_Conf_Message(int s, int source, void *dummy) {
    // buffer for incoming message
    char buffer[MAX_FRAGMENT_SIZE + sizeof(conf_fragment)];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    // receive the message
    int ret = spines_recvfrom(s, buffer, sizeof(buffer), 0, (struct sockaddr *)&from_addr, &from_len);
    if (ret <= 0) return;

    // ensure enough bytes for full header
    if (ret < sizeof(conf_fragment)) {
        Alarm(DEBUG, "Receiver: Received fragment too small\n");
        return;
    }

    // interpret header and extract the payload and length
    conf_fragment *hdr = (conf_fragment *)buffer;
    char *payload = buffer + sizeof(conf_fragment);
    size_t payload_len = ret - sizeof(conf_fragment);

    //on first fragment initialize buffers and tracking of total frags
    if (expected_fragments == -1) {
        expected_fragments = hdr->total_fragments;
        Conf_ID = hdr->conf_id;
        Alarm(DEBUG, "Receiver: Expecting %d fragments for conf ID %u\n", expected_fragments, Conf_ID);
        fragment_data = calloc(expected_fragments, sizeof(char *));
        fragment_lens = calloc(expected_fragments, sizeof(size_t));
    }

    // check for mismatched config ID or out of bounds
    if (hdr->conf_id != Conf_ID || hdr->fragment_index >= expected_fragments) {
        Alarm(DEBUG, "Receiver: Unexpected conf_id or fragment index\n");
        return;
    }

    // check for duplicates
    if (fragment_data[hdr->fragment_index] != NULL) {
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
    if (received_fragments == expected_fragments) {
        Alarm(PRINT, "Receiver: All %d fragments received. Assembling config...\n", expected_fragments);
        Assemble_Config();
    }
}

static void Assemble_Config(void)
{
    // compute the total message length
    size_t total_len = 0;
    for (int i = 0; i < expected_fragments; i++) {
        total_len += fragment_lens[i];
    }

    // allocate a buffer to hold config
    char *assembled = malloc(total_len + 1);
    size_t offset = 0;

    // copy each fragment into buffer
    for (int i = 0; i < expected_fragments; i++) {
        memcpy(assembled + offset, fragment_data[i], fragment_lens[i]);
        offset += fragment_lens[i];
        free(fragment_data[i]);
    }
    assembled[total_len] = '\0';
    free(fragment_data);
    free(fragment_lens);

    Alarm(PRINT, "Receiver: Assembled config size = %lu bytes\n", total_len);

    // validate message contains at least a signature length field
    if (total_len < sizeof(uint32_t)) {
        Alarm(PRINT, "Receiver: Total size too small to contain signature length\n");
        free(assembled);
        return;
    }

    // Read the 4-byte signature length
    uint32_t sig_len = 0;
    memcpy(&sig_len, assembled, sizeof(uint32_t));
    // Ensure there is enough data for the signature and the YAML config
    if (total_len < sizeof(uint32_t) + sig_len) {
        Alarm(PRINT, "Receiver: Signature length invalid or incomplete message\n");
        free(assembled);
        return;
    }

    // Pointers to the signature and the YAML data
    unsigned char *raw_sig = (unsigned char *)(assembled + sizeof(uint32_t));
    char *yaml_data = (char *)(raw_sig + sig_len);
    size_t yaml_len = total_len - sizeof(uint32_t) - sig_len;

    // Load public key to verify the signature
    EVP_PKEY *cm_pub_key = load_key_from_file("cm_keys/public_key.pem", 0);
    if (!cm_pub_key) {
        Alarm(PRINT, "Receiver: Failed to load public key\n");
        free(assembled);
        return;
    }

    // Verify the signature against the YAML data
    int valid = verify_buffer((unsigned char *)yaml_data, yaml_len, raw_sig, sig_len, cm_pub_key);
    Alarm(PRINT, "Receiver: Signature is %s\n", valid == 0 ? "VALID" : "INVALID");

    // cleanup
    free(raw_sig);
    free(assembled);
}
