project('MangoHud',
  ['c', 'cpp'],
  version : 'v0.8.3',
  license : 'MIT',
  meson_version: '>=0.60.0',
  default_options : ['buildtype=release', 'c_std=c99', 'cpp_std=c++17', 'warning_level=2']
)

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

prog_python = import('python').find_installation('python3', modules: ['mako'])
null_dep = dependency('', required : false)

mangohud_version = vcs_tag(
  command: ['git', 'describe', '--tags', '--dirty=+'],
  input:  'version.h.in',
  output: 'version.h')

mangohud_version_dep = declare_dependency(sources : mangohud_version)

pre_args = [
  '-D__STDC_CONSTANT_MACROS',
  '-D__STDC_FORMAT_MACROS',
  '-D__STDC_LIMIT_MACROS',
  '-DPACKAGE_VERSION="@0@"'.format(meson.project_version()),
  '-DSPDLOG_COMPILED_LIB'
]

if get_option('buildtype') == 'debug'
  pre_args += '-DDEBUG'
endif

# Always set max spdlog level, handle this using MANGOHUD_LOG_LEVEL instead.
pre_args += '-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE'

if get_option('glibcxx_asserts')
  pre_args += '-D_GLIBCXX_ASSERTIONS'
endif

# Check for GCC style atomics
if cc.compiles('''#include <stdint.h>
                  int main() {
                    struct {
                      uint64_t *v;
                    } x;
                    return (int)__atomic_load_n(x.v, __ATOMIC_ACQUIRE) &
                           (int)__atomic_add_fetch(x.v, (uint64_t)1, __ATOMIC_ACQ_REL);

                  }''',
               name : 'GCC atomic builtins')
  pre_args += '-DUSE_GCC_ATOMIC_BUILTINS'
endif

# Not in C99, needs POSIX
if cc.compiles('''
                 #include <time.h>
                  int main() {
                    struct timespec ts;
                    return timespec_get(&ts, TIME_UTC);

                  }''',
               name : 'Supports timespec_get')
  pre_args += '-DHAVE_TIMESPEC_GET'
endif

# Check for GCC style builtins
foreach b : ['bswap32', 'bswap64', 'clz', 'clzll', 'ctz', 'expect', 'ffs',
             'ffsll', 'popcount', 'popcountll', 'unreachable']
  if cc.has_function(b)
    pre_args += '-DHAVE___BUILTIN_@0@'.format(b.to_upper())
  endif
endforeach

vulkan_wsi_args = ['-DVK_USE_PLATFORM_WIN32_KHR']
vulkan_wsi_deps = []

inc_common = [
    include_directories('include'),
]

dep_pthread = dependency('threads')

add_project_arguments(
  cc.get_supported_arguments([
    '-Werror=implicit-function-declaration',
    '-Werror=missing-declarations',
    '-Werror=missing-prototypes',
    '-Werror=return-type',
    '-Werror=incompatible-pointer-types',
    '-Wno-unused-parameter',
    '-Qunused-arguments',
    '-fno-math-errno',
    '-fno-trapping-math',
    '-Wno-missing-field-initializers',
  ]), language : ['c'],
)

add_project_arguments(
  cpp.get_supported_arguments([
    '-Werror=missing-declarations',
    '-Werror=return-type',
    '-Wno-unused-parameter',
    '-Qunused-arguments',
    '-fno-math-errno',
    '-fno-trapping-math',
    '-Wno-non-virtual-dtor',
    '-Wno-missing-field-initializers',
  ]), language : ['cpp'],
)

foreach a : pre_args
  add_project_arguments(a, language : ['c', 'cpp'])
endforeach

dep_dl = null_dep
dep_rt = null_dep

vulkan_headers_sp = subproject('vulkan-headers')
vulkan_headers_api_xml = vulkan_headers_sp.get_variable('vulkan_api_xml')
dep_vulkan_headers = vulkan_headers_sp.get_variable('vulkan_headers_dep')

vulkan_utility_libraries_sp = subproject('vulkan-utility-libraries')
dep_vulkan_utility_libraries = vulkan_utility_libraries_sp.get_variable('vulkan_utility_libraries_dep')

vk_enum_to_str = custom_target(
  'vk_enum_to_str',
  input : ['bin/gen_enum_to_str.py', vulkan_headers_api_xml],
  output : ['vk_enum_to_str.c', 'vk_enum_to_str.h', 'vk_enum_defines.h'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@',
    '--out-c', '@OUTPUT0@',
    '--out-h', '@OUTPUT1@',
    '--out-d', '@OUTPUT2@',
    '--beta', 'false'
  ],
)

vk_dispatch_table = custom_target(
  'vk_dispatch_table',
  input : ['bin/vk_dispatch_table_gen.py', vulkan_headers_api_xml],
  output : ['vk_dispatch_table.c', 'vk_dispatch_table.h'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@',
    '--out-c', '@OUTPUT0@', '--out-h', '@OUTPUT1@',
    '--beta', 'false'
  ],
)

vk_extensions = custom_target(
  'vk_extensions',
  input : ['bin/vk_extensions_gen.py', vulkan_headers_api_xml],
  output : ['vk_extensions.c', 'vk_extensions.h'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@',
    '--out-c', '@OUTPUT0@', '--out-h', '@OUTPUT1@'
  ],
)

imgui_options = [
  'default_library=static',
  'werror=false',
  'dx9=disabled',
  'dx10=disabled',
  'dx11=enabled',
  'dx12=enabled',
  'metal=disabled',
  'opengl=disabled',
  'vulkan=disabled',
  'glfw=disabled',
  'sdl2=disabled',
  'sdl3=disabled',
  'osx=disabled',
  'win=enabled',
  'allegro5=disabled',
  'webgpu=disabled',
  'sdl2_renderer=disabled',
  'sdl3_renderer=disabled',
]

sizeof_ptr = cc.sizeof('void*')
if sizeof_ptr == 8
  pre_args += '-DMANGOHUD_ARCH="64bit"'
elif sizeof_ptr == 4
  pre_args += '-DMANGOHUD_ARCH="32bit"'
endif

imgui_sp = subproject('imgui', default_options: imgui_options)
dearimgui_dep = imgui_sp.get_variable('imgui_dep')

meson.override_dependency('imgui', dearimgui_dep)

implot_sp = subproject('implot', default_options: ['default_library=static'])
implot_dep = implot_sp.get_variable('implot_dep')

spdlog_options = [
  'default_library=static',
  'compile_library=true',
  'werror=false',
  'tests=disabled',
  'external_fmt=disabled',
  'std_format=disabled'
]

spdlog_dep = dependency('spdlog', required: false)

if get_option('use_system_spdlog').disabled() or not spdlog_dep.found()
    if get_option('use_system_spdlog').enabled()
      warning('spdlog dependency not found following back to submodule')
    endif
    spdlog_sp = subproject('spdlog', default_options: spdlog_options)
    spdlog_dep = spdlog_sp.get_variable('spdlog_dep')
endif

# Windows dependencies
minhook_dep = dependency('minhook', fallback: ['minhook', 'minhook_dep'], required: true)
windows_deps = [
  minhook_dep,
]

# Windows system libraries
dep_d3d11 = cc.find_library('d3d11', required: false)
dep_d3d12 = cc.find_library('d3d12', required: false)
dep_dxgi = cc.find_library('dxgi', required: false)
dep_psapi = cc.find_library('psapi', required: false)
dep_iphlpapi = cc.find_library('iphlpapi', required: false)

subdir('src')

if get_option('include_doc')
  subdir('data')
endif

if get_option('tests').enabled()
endif
