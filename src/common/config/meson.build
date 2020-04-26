# Generate a file containing the last annotated git tag and possibly also the
# amount of commits since then and the hash of the last commit
version_header = vcs_tag(
  command : ['git', 'describe', '--always'],
  input : 'version.h.in',
  output : 'version.h',
  replace_string : '@VCS_VERSION@'
)
