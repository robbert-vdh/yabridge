# Contains constants determined while configuring the build. As an alternative
# to preprocessor macros.
config_header = configure_file(
  input : 'config.h.in',
  output : 'config.h',
  configuration : configuration_data(
    {
      'individual_host_binary_32bit': individual_host_name_32bit + '.exe',
      'individual_host_binary_64bit': individual_host_name_64bit + '.exe',
      'group_host_binary_32bit': group_host_name_32bit + '.exe',
      'group_host_binary_64bit': group_host_name_64bit + '.exe',
    }
  )
)

# Generate a file containing the last annotated git tag, the amount of commits
# since then, and the hash of the last commit
# NOTE: We explicitly specify the git directory to be relative to the main
#       `meson.build` file. Otherwise git will search up the file tree, which
#       might cause `git describe` to be run in an unrelated repository when
#       building from a tarball that's been extracted inside of a  git
#       directory, like when building the `yabridge` AUR package. In that case
#       `vcs_tag()` should always fall back to the Meson project version.
version_header = vcs_tag(
  command : ['git', '--git-dir=' + (meson.project_source_root() / '.git'), 'describe', '--always'],
  input : 'version.h.in',
  output : 'version.h',
  replace_string : '@VCS_VERSION@'
)

configuration_dep = declare_dependency(
  include_directories : include_directories('.'),
  sources : [config_header, version_header],
)
