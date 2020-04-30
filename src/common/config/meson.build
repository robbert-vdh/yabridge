# Contains constants determined while configuring the build. As an alternative
# to preprocessor macros.
config_header = configure_file(
  input : 'config.h.in',
  output : 'config.h',
  configuration : configuration_data(
    {
      'host_binary_32bit': host_name_32bit + '.exe',
      'host_binary_64bit': host_name_64bit + '.exe',
    }
  )
)

# Generate a file containing the last annotated git tag and possibly also the
# amount of commits since then and the hash of the last commit
version_header = vcs_tag(
  command : ['git', 'describe', '--always'],
  input : 'version.h.in',
  output : 'version.h',
  replace_string : '@VCS_VERSION@'
)
