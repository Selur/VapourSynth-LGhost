project('LGhost', 'cpp',
  default_options : ['buildtype=release', 'b_ndebug=if-release', 'cpp_std=c++17'],
  meson_version : '>=0.51.0',
  license : 'GPL-3.0',
  version : '1.1.0'
)

# Prefer system VapourSynth headers, fall back to the vendored API 4 headers.
system_vs_dep = dependency('vapoursynth', version : '>=55', required : false)
if system_vs_dep.found()
  vapoursynth_dep = system_vs_dep.partial_dependency(compile_args : true, includes : true)
else
  vapoursynth_dep = declare_dependency(include_directories : include_directories('include/vapoursynth'))
endif

cxx = meson.get_compiler('cpp')
msvc = cxx.get_argument_syntax() == 'msvc'
if not msvc
  add_project_arguments('-O3', '-fno-math-errno', '-fno-trapping-math', language : 'cpp')
endif

sources = ['LGhost/LGhost.cpp']
libs = []
cpu = host_machine.cpu_family()

if cpu in ['x86', 'x86_64']
  # SSE2 is the x86-64 baseline; AVX2 and AVX-512 are picked at runtime by instrset_detect().
  add_project_arguments('-DLGHOST_X86', language : 'cpp')
  if not msvc
    add_project_arguments('-mfpmath=sse', '-msse2', language : 'cpp')
  endif
  sources += ['LGhost/LGhost_SSE2.cpp', 'LGhost/VCL2/instrset_detect.cpp']

  libs += static_library('avx2', 'LGhost/LGhost_AVX2.cpp',
    dependencies : vapoursynth_dep,
    cpp_args : msvc ? ['-DLGHOST_X86', '/arch:AVX2'] : ['-DLGHOST_X86', '-mavx2', '-mfma'],
    pic : true,
    gnu_symbol_visibility : 'hidden'
  )

  libs += static_library('avx512', 'LGhost/LGhost_AVX512.cpp',
    dependencies : vapoursynth_dep,
    cpp_args : msvc ? ['-DLGHOST_X86', '/arch:AVX512'] : ['-DLGHOST_X86', '-mavx512f', '-mavx512vl', '-mavx512bw', '-mavx512dq', '-mfma'],
    pic : true,
    gnu_symbol_visibility : 'hidden'
  )
elif cpu in ['aarch64', 'arm']
  # The SSE2 path through sse2neon (VCL2/instrset.h).
  if not msvc
    add_project_arguments('-funroll-loops', language : 'cpp')
  endif
  sources += ['LGhost/LGhost_SSE2.cpp', 'LGhost/VCL2/instrset_detect.cpp']
endif

# VapourSynth only autoloads *.dylib on macOS, while Meson defaults to *.so for modules.
suffix = host_machine.system() == 'darwin' ? 'dylib' : []

if get_option('python_package')
  # Same layout as the VapourSynth PyPI package autoloads from: site-packages/vapoursynth/plugins
  py = import('python').find_installation(pure : false)
  install_dir = join_paths(py.get_install_dir(), 'vapoursynth', 'plugins')
else
  libdir = join_paths(get_option('prefix'), get_option('libdir'))
  if system_vs_dep.found()
    libdir = system_vs_dep.get_variable(pkgconfig : 'libdir', default_value : libdir)
  endif
  install_dir = join_paths(libdir, 'vapoursynth')
endif

shared_module('lghost', sources,
  dependencies : vapoursynth_dep,
  link_with : libs,
  install : true,
  install_dir : install_dir,
  name_suffix : suffix,
  gnu_symbol_visibility : 'hidden'
)
