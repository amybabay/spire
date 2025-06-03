#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <openssl/sha.h>
#include <openssl/bn.h>
#include "tc_wrapper.h"
#include "parser.h"
#include "key_generation.h"
#include "../OpenTC-1.1/TC-lib-1.0/TC.h"
#include "config_utils.h"

static void Print_Usage(void)
{
    fprintf(stderr,
            "Usage:\n"
            "  ./config_generator <input_yaml> <output_yaml>\n\n"
            "Arguments:\n"
            "  input_yaml     Path to input YAML config file\n"
            "  output_yaml    Path to output signed config file\n");
    exit(EXIT_FAILURE);
}

static void Usage(int argc, char **argv, const char **input_yaml, const char **output_yaml)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input_yaml> <output_yaml>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    *input_yaml = argv[1];
    *output_yaml = argv[2];
}

int main(int argc, char *argv[])
{
    int simulate_tpm = 0;
    const char *input_path = NULL;
    const char *output_path = NULL;

    Usage(argc, argv, &input_path, &output_path);

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