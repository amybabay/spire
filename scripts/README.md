# Automatically Generating Configuration Files

This is still a bit of a work in progress, but the goal is to automatically
generate all the Spire configuration files (i.e. the contents of a directory
like `spire/example_conf/conf_4`, along with a Docker compose file) based on a
single JSON config file.

## Usage

```
python3 gen_conf.py <path_to_json_config>
```

The config file includes a `conf_suffix` field. The script will generate its
output in `conf/conf_<conf_suffix>`.

It uses the files in the `base_files` directory as the base for editing and
generating its own files. Currently, if you change those file formats in Spire,
you also need to update the base files (possible TODO: make it work directly
off of Spire's default configs in the existing directory structure).

## Examples

Example JSON configurations for 4 and 6 replicas are provided in
`conf/conf_4.json` and `conf/conf_6.json`.
