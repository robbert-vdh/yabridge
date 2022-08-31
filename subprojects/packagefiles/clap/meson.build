project('clap', 'cpp', version : '1.1.1')

# Without the `is_system` we'd get warnings about `CLAP_EXPORT` on `clap_entry`
# not doing anything.
clap_dep = declare_dependency(include_directories : include_directories('include', is_system : true))
