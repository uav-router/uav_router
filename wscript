#! /usr/bin/env python
# encoding: utf-8

import os
import platform

from waflib.Scripting import distclean as original_distclean
import waflib
import hashlib

VERSION='0.0.1'
APPNAME='uav-router'

# these variables are mandatory ('/' are converted automatically)
top = '.'
out = 'build_'+platform.machine()
yaml_out = out+'/yaml-cpp'
deps_install = out+'/deps_install'

from waflib.Tools.compiler_cxx import cxx_compiler
cxx_compiler['linux'] = ['clang++','g++']

def dinfo(ctx):
    ret = ctx.exec_command("mkdir -p debugsym", shell=True)
    if ret: return ret
    ret = ctx.exec_command('llvm-objcopy --only-keep-debug {0}/uav-router debugsym/uav-router.debug'.format(out), shell=True)
    if ret: return ret
    h = hashlib.new('md5')
    h.update(open('debugsym/uav-router.debug','rb').read())
    return ctx.exec_command("mv debugsym/uav-router.debug debugsym/{}.debug".format(h.hexdigest()), shell=True)
        
def bool_opt(opt):
    return opt.upper()=="TRUE"

def upload_sym(ctx):
    return ctx.exec_command("docker run --rm -v {}:/work getsentry/sentry-cli upload-dif debugsym/*".format(ctx.path), shell=True)

def options(opt):
    opt.add_option('--sentry', action='store', default="no", type="choice", choices=["yes","no"], help='sentry integration')
    opt.add_option('--yaml', action='store', default="yes", type="choice", choices=["yes","no"], help='with yaml configuration')
    opt.add_option('--install_lib', action='store', default="no", type="choice", choices=["yes","no"], help='library installation')
    opt.add_option('--build_tests', action='store', default="no", type="choice", choices=["yes","no"], help='build tests')
    opt.add_option('--deps-target', action='store', default="native", type="choice", choices=["arm32","arm64","native"], help='target platform to build dependencies')
    opt.load('compiler_cxx')
    opt.load('clangxx_cross')

def distclean(ctx):
    original_distclean(ctx)
    sentry_dir = ctx.path.make_node('dependencies/sentry-native-0.4.10')
    if sentry_dir.exists():
        sentry_dir.delete()

def configure(conf):
    conf.load('compiler_cxx')
    conf.load('clangxx_cross')
    conf.load('clang_compilation_database')
    conf.env.CPPFLAGS = ['-g','-std=c++17']
    conf.env.append_value('LDFLAGS',['-Wl,--build-id=sha1','-fuse-ld=lld'])
    conf.env.SENTRY = conf.options.sentry
    conf.env.YAML = conf.options.yaml
    
def build_deps(conf):
    toolchain = None
    if conf.options.deps_target=='arm32':
        toolchain = conf.path.find_node('docker/cmake-toolchains/arm32.toolchain')
    elif conf.options.deps_target=='arm64':
        toolchain = conf.path.find_node('docker/cmake-toolchains/arm64.toolchain')
    install_dir = conf.path.make_node(deps_install)
    if not install_dir.exists():
        install_dir.mkdir()
    build_dir = conf.path.make_node(yaml_out)
    if not build_dir.exists():
        print('Build yaml-cpp dependency...')
        build_dir.mkdir()
        print('Run cmake...')
        cmdline = 'cmake -S dependencies/yaml-cpp -B {} -DYAML_CPP_BUILD_TESTS=OFF'.format(build_dir.abspath())
        if toolchain: cmdline += ' -DCMAKE_TOOLCHAIN_FILE={}'.format(toolchain.abspath())
        ret = conf.exec_command(cmdline)
        if ret: conf.fatal('cmake error %i'%ret)
        print('Run make...')
        ret = conf.exec_command(
            ['make','-C', build_dir.abspath()],
        )
        if ret: conf.fatal('make error %i'%ret)
        ret = conf.exec_command(['make','DESTDIR={}'.format(install_dir.abspath()),'install','-C', build_dir.abspath()])
        if ret: conf.fatal('make install error %i'%ret)
        print('Build yaml-cpp complete')
    print('Build sentry dependency ...')
    build_dir = conf.path.make_node(out+'/sentry')
    if not build_dir.exists():
        build_dir.mkdir()
        print('Run cmake...')
        cmdline = 'cmake -B {} -Sdependencies/sentry -DSENTRY_BUILD_TESTS=OFF -DSENTRY_BUILD_EXAMPLES=OFF'.format(build_dir.abspath())
        if toolchain: cmdline += ' -DCMAKE_TOOLCHAIN_FILE={}'.format(toolchain)
        ret = conf.exec_command(cmdline)
        if ret: conf.fatal('cmake error %i'%ret)
        print('Run make...')
        ret = conf.exec_command(['make','-C',build_dir.abspath()])
        if ret: conf.fatal('make error %i'%ret)
        print('Run make install...')
        ret = conf.exec_command(['make','DESTDIR={}'.format(install_dir.abspath()),'install','-C', build_dir.abspath()])
        if ret: conf.fatal('make install error %i'%ret)
        print('Build sentry complete')

def dockerize(conf):
    output = conf.path.make_node(out+'/docker_image')
    if output.exists():
        output.delete()
    output.mkdir()
    print(conf.options.prefix)
    print('Run dockerize...')
    executable_path = conf.options.prefix + '/bin/uav-router'
    ret = conf.exec_command(['dockerize', '--output-dir', output.abspath(), '-n', '-e', executable_path, executable_path])
    if ret: conf.fatal('dockerize error %i'%ret)

def image(conf):
    output = conf.path.find_node(out)
    app_node = output.find_node('uav-router')
    if app_node is None:
        conf.fatal('no uav-router exists')
    cout = conf.cmd_and_log(['readelf', '-h', app_node.abspath()])
    elf_hdr = {}
    for line in cout.splitlines():
        k,v = line.split(':')
        elf_hdr[k.strip()] = v.strip()
    print(elf_hdr['Machine'])#ARM,Advanced Micro Devices X86-64,AArch64
    arch_params = {
        'ARM':{
            'libs_list':'docker/stdlibs/lib-arm32.txt',
            'libs_dir_input':'/sysroot/arm32/lib/',
            'libs_dir_output':'sysroot/lib'
        },
        'Advanced Micro Devices X86-64':{
            'libs_list':'docker/stdlibs/lib-x86.txt',
            'libs_dir_input':'/lib64/',
            'libs_dir_output':'sysroot/lib64'
        },
        'AArch64':{
            'libs_list':'docker/stdlibs/lib-arm64.txt',
            'libs_dir_input':'/sysroot/arm64/lib64/',
            'libs_dir_output':'sysroot/lib64'
        }
    }
    params = arch_params[elf_hdr['Machine']]
    
    #copy template
    image_dir = conf.path.make_node('docker_image')
    if image_dir.exists():
        print('delete image directory {}'.format(image_dir.abspath()))
        image_dir.delete()
    input = conf.path.find_node('docker/cross/docker_image')
    ret = conf.exec_command(['cp', '-r', input.abspath(), conf.path.abspath()])
    if ret: conf.fatal('copy image template error %i'%ret)
    sysroot = image_dir.make_node('sysroot')

    #copy sentry.so
    sentry_so = output.find_node('deps_install/usr/local/lib/libsentry.so')
    if sentry_so is None:
        sentry_so = output.find_node('deps_install/usr/local/lib64/libsentry.so')
    if sentry_so is None:
        conf.fatal('no sentry.so exists')
    libs_dir = image_dir.make_node(params['libs_dir_output'])
    if not libs_dir.exists():
        libs_dir.mkdir()
    ret = conf.exec_command(['cp', sentry_so.abspath(), libs_dir.abspath()])
    if ret: conf.fatal('copy sentry.so error %i'%ret)

    #copy application
    ret = conf.exec_command(['mkdir', '-p', sysroot.abspath()+'/usr/bin'])
    if ret: conf.fatal('mkdir for app error %i'%ret)
    ret = conf.exec_command(['cp', app_node.abspath(), sysroot.find_node('usr/bin').abspath()])
    if ret: conf.fatal('copy uav-router error %i'%ret)

    #copy .so dependencies
    source_libs_dir = params['libs_dir_input']
    for lib_name in conf.path.find_node(params['libs_list']).read().splitlines():
        ret = conf.exec_command(['cp', '-L', source_libs_dir+lib_name, libs_dir.abspath()])
        if ret: conf.fatal('copy uav-router error %i'%ret)

    if elf_hdr['Machine']=='AArch64':
        lib = sysroot.make_node('lib')
        lib.mkdir()
        ret = conf.exec_command(['ln', '-s', "../lib64/ld-linux-aarch64.so.1", lib.abspath()])
        if ret: conf.fatal('copy uav-router error %i'%ret)


def build(bld):
    print('sentry\t- %r' % bld.env.SENTRY)
    print('yaml\t- %r' % bld.env.YAML)
    print('Build command: %s' % bld.cmd)
    print('Prefix: %s' % bld.env.PREFIX)
    base_src = [ bld.path.find_node('src/err.cpp'),
                bld.path.find_node('src/log.cpp')]
    io_src =  [src for src in bld.path.find_node('src').ant_glob('*.cpp') if src not in base_src]
    tests = bld.path.find_node('tests').ant_glob('*.cpp')
    app = bld.path.find_node('app').ant_glob('*.cpp')
    
    libs = ['anl','udev','avahi-common','avahi-client','avahi-core']
    defs = []
    libpath = []
    incs = []
    deps_libs = bld.path.find_node(deps_install+'/usr/local/lib')
    libinst = 'lib'
    if not deps_libs: 
        deps_libs = bld.path.find_node(deps_install+'/usr/local/lib64')
        libinst = 'lib64'
    if bld.env.YAML=='yes':
        libs.append('yaml-cpp')
        defs.append('YAML_CONFIG')
        libpath.append(deps_libs.abspath())
        incs.append(deps_install+'/usr/local/include')
    if bld.env.SENTRY=='yes':
        libs.append('sentry')
        defs.append('USING_SENTRY')
        commit_hash=bld.cmd_and_log('git rev-parse --short HEAD', output=waflib.Context.STDOUT)
        defs.append('GIT_COMMIT=\"{}\"'.format(commit_hash[:-1]))
        if bld.env.YAML!='yes':
            libpath.append(deps_libs.abspath())
            incs.append(deps_install+'/usr/local/include')
        bld.install_files(libinst,deps_libs.ant_glob('*.so'))
    lib_path=None
    if bld.options.install_lib == 'yes':
        lib_path='lib'
        bld.install_files('include/uavlib',bld.path.find_node('src').ant_glob('*.h'))
        bld.install_files('include/uavlib/inc',bld.path.find_node('src/inc').ant_glob('*.h'))

    bld.stlib(
        source   = base_src,
        target   = 'uavr-base',
        defines  = defs,
        includes = incs,
        install_path = lib_path
    )
    bld.stlib(
        source   = io_src,
        target   = 'uavr-io',
        defines  = defs,
        includes = incs,
        install_path = lib_path
    )
    incs.append('src')
    if bld.options.build_tests == 'yes':
        for test in tests:
            bld.program(
                source       = [test],
                use          = ['uavr-base','uavr-io'],
                target       = os.path.splitext(test.name)[0],
                includes     = incs,
                defines      = defs,
                lib          = libs,
                libpath      = libpath,
                install_path = None
            )
    if bld.env.YAML=='yes':
        bld.program(
            source       = app,
            use          = ['uavr-base','uavr-io'],
            target       = 'uav-router',
            includes     = incs,
            defines      = defs,
            lib          = libs,
            libpath      = libpath
        )
        bld.add_post_fun(dinfo)
        
    else:
        print('uav-router is built with yamp-cpp dependency only')

# cross build
# clang++ --target=arm-linux-gnueabihf --rtlib=compiler-rt --sysroot=sysroot/fedora-arm32 -fuse-ld=lld hello.cpp -o hello
# clang++ --target=aarch64-linux-gnu --rtlib=compiler-rt --sysroot=sysroot/fedora-arm64 -fuse-ld=lld hello.cpp -o hello
# cmake -DSENTRY_BUILD_TESTS=OFF -DSENTRY_BUILD_EXAMPLES=OFF -DCMAKE_TOOLCHAIN_FILE=../arm64.toolchain ../../../dependencies/sentry-native-0.4.10/
# cmake -DCMAKE_TOOLCHAIN_FILE=../arm64.toolchain -DYAML_CPP_BUILD_TESTS=OFF ../../../dependencies/yaml-cpp/
# waf configure --clangxx-target-triple=arm-linux-gnueabihf --clangxx-sysroot=/sysroot/arm32/
