from ruamel.yaml import YAML
import sys

def scrub_keys(yaml_data):
    # Top-level service keys
    if 'service_keys' in yaml_data:
        for k in yaml_data['service_keys']:
            yaml_data['service_keys'][k] = ""

    # Per-site and per-host keys
    for site in yaml_data.get('sites', []):
        for host in site.get('hosts', []):
            host['permanent_public_key'] = ""
            host['spines_internal_public_key'] = ""
            host['spines_external_public_key'] = ""
            host['encrypted_spines_internal_private_key'] = ""
            host['encrypted_spines_external_private_key'] = ""

        for replica in site.get('replicas', []):
            replica['instance_public_key'] = ""
            replica['encrypted_instance_private_key'] = ""
            replica['encrypted_prime_threshold_key_share'] = ""
            replica['encrypted_sm_threshold_key_share'] = ""

    return yaml_data

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python scrub_keys.py <input.yaml> <output.yaml>")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]

    yaml = YAML()
    yaml.preserve_quotes = True
    yaml.width = 4096  # Prevent wrapping

    with open(input_file, 'r') as f:
        data = yaml.load(f)

    data = scrub_keys(data)

    with open(output_file, 'w') as f:
        yaml.dump(data, f)

    print(f"Scrubbed key values written to {output_file}")
