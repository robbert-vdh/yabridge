project('ara', version : '2.3')

# The ARA API is headers-only. The root of the repository contains ARAVST3.h,
# ARAInterface.h, etc. directly, so we expose the root as the include directory.
ara_include_dir = include_directories('.')

ara_dep = declare_dependency(include_directories : ara_include_dir)
