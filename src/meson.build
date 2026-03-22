glslang = find_program('glslang', 'glslangValidator')

# Windows-only build - no Linux LD_PRELOAD paths needed
libdir_mangohud = join_paths(get_option('prefix'), get_option('libdir'))

git = find_program('git', required: false)
if git.found()
  git_describe = run_command([git, 'describe', '--tags', '--dirty=+'], check: false)
endif
if git.found() and git_describe.returncode() == 0
  describe_ver = git_describe.stdout().strip()
else
  describe_ver = meson.project_version()
endif

conf_data = configuration_data()
conf_data.set('ld_libdir_mangohud_abs', libdir_mangohud)
conf_data.set('ld_libdir_mangohud', libdir_mangohud)
conf_data.set('cpu_family', host_machine.cpu_family())
conf_data.set('version', describe_ver)

overlay_shaders = [
  'overlay.frag',
  'overlay.vert',
]
overlay_spv = []
foreach s : ['overlay.frag', 'overlay.vert']
  overlay_spv += custom_target(
    s + '.spv.h', input : s, output : s + '.spv.h',
    command : [glslang, '-V', '-x', '-o', '@OUTPUT@', '@INPUT@'])
endforeach

util_files = files(
  'mesa/util/os_socket.c',
  'mesa/util/os_time.c',
)

# Core files shared between all platforms
vklayer_files = files(
  'hud_elements.cpp',
  'overlay.cpp',
  'overlay_params.cpp',
  'font.cpp',
  'keybinds.cpp',
  'font_unispace.c',
  'logging.cpp',
  'config.cpp',
  'gpu.cpp',
  'blacklist.cpp',
  'file_utils.cpp',
  'nvidia.cpp',
)

# Windows-specific source files
vklayer_files += files(
  'dxgi_gpu.cpp',
  'file_utils_win32.cpp',
  'cpu_win32.cpp',
  'memory_win32.cpp',
  'battery_win32.cpp',
  'net_win32.cpp',
  'iostats_win32.cpp',
  'notify_win32.cpp',
  'nvapi.cpp',
  'adl.cpp',
  'win/dxgi.cpp',
  'win/main.cpp',
  'win/kiero.cpp',
  'win/d3d12_hook.cpp',
  'win/d3d11_hook.cpp',
  'win/d3d_shared.cpp',
)

# NVML support (works on Windows via nvml.dll)
nvml_h_found = get_option('with_nvml') == 'enabled'
if get_option('with_nvml') == 'system'
  nvml_h_found = cc.has_header('nvml.h')
  if not nvml_h_found
    error('nvml.h was not found. Disable with \'-Dwith_nvml=disabled\' if gpu stats by NVML are not needed.')
  endif
  pre_args += '-DUSE_SYSTEM_NVML'
endif

if nvml_h_found
  pre_args += '-DHAVE_NVML'
  pre_args += '-DNVML_NO_UNVERSIONED_FUNC_DEFS'
  vklayer_files += files(
    'loaders/loader_nvml.cpp',
  )
endif

link_args = cc.get_supported_link_arguments(['-static-libstdc++', '-static-libgcc'])

mangohud_static_lib = static_library(
  'MangoHud',
  mangohud_version,
  files('vulkan.cpp'),
  util_files,
  vk_enum_to_str,
  vk_dispatch_table,
  vk_extensions,
  vklayer_files,
  overlay_spv,
  c_args : [
    pre_args,
    vulkan_wsi_args
    ],
  cpp_args : [
    pre_args,
    vulkan_wsi_args
    ],
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    mangohud_version_dep,
    vulkan_wsi_deps,
    dearimgui_dep,
    spdlog_dep,
    dep_dl,
    dep_rt,
    dep_pthread,
    dep_vulkan_headers,
    dep_vulkan_utility_libraries,
    windows_deps,
    implot_dep,
    dep_d3d11,
    dep_d3d12,
    dep_dxgi,
    dep_psapi,
    dep_iphlpapi],
  include_directories : [inc_common],
  link_args : link_args,
  install_dir : libdir_mangohud,
  install : false
)

mangohud_shared_lib = shared_library(
  'MangoHud',
  objects: mangohud_static_lib.extract_all_objects(),
  link_with: mangohud_static_lib,
  link_args : link_args,
  install_dir : libdir_mangohud,
  install: true
)

configure_file(input : 'mangohud.json.in',
  output : '@0@.@1@.json'.format(meson.project_name(), host_machine.cpu_family()),
  configuration : conf_data,
  install : true,
  install_dir : join_paths(get_option('datadir'), 'vulkan', 'implicit_layer.d'),
  install_tag : 'runtime',
)

# MangoHud standalone overlay (windowed mode, default)
dep_dwmapi = cc.find_library('dwmapi', required: true)

mangohud_overlay = executable(
  'MangoHud',
  files('app/mangohud_overlay.cpp'),
  c_args : [pre_args, vulkan_wsi_args],
  cpp_args : [pre_args, vulkan_wsi_args],
  gui_app : false,
  link_with: mangohud_static_lib,
  dependencies : [
    mangohud_version_dep,
    vulkan_wsi_deps,
    dearimgui_dep,
    spdlog_dep,
    dep_dl,
    dep_rt,
    dep_pthread,
    dep_vulkan_headers,
    dep_vulkan_utility_libraries,
    windows_deps,
    implot_dep,
    dep_d3d11,
    dep_d3d12,
    dep_dxgi,
    dep_psapi,
    dep_iphlpapi,
    dep_dwmapi],
  include_directories : [inc_common],
  link_args : link_args,
  install: true,
)

# DXGI proxy DLL - drop next to game exe for DirectX overlay (fullscreen mode)
mangohud_proxy = shared_library(
  'dxgi',
  files('proxy/dxgi_proxy.cpp'),
  c_args : [pre_args, vulkan_wsi_args],
  cpp_args : [pre_args, vulkan_wsi_args],
  link_with: mangohud_static_lib,
  dependencies : [
    mangohud_version_dep,
    vulkan_wsi_deps,
    dearimgui_dep,
    spdlog_dep,
    dep_dl,
    dep_rt,
    dep_pthread,
    dep_vulkan_headers,
    dep_vulkan_utility_libraries,
    windows_deps,
    implot_dep,
    dep_d3d11,
    dep_d3d12,
    dep_dxgi,
    dep_psapi,
    dep_iphlpapi],
  include_directories : [inc_common],
  vs_module_defs: 'proxy/dxgi_proxy.def',
  name_prefix: '',
  link_args : link_args,
  install: true,
  install_dir: libdir_mangohud,
)
