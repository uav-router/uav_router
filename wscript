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
    ret = ctx.exec_command('objcopy --only-keep-debug {0}/uav-router debugsym/uav-router.debug'.format(out), shell=True)
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
    opt.load('compiler_cxx')

def distclean(ctx):
    original_distclean(ctx)
    sentry_dir = ctx.path.make_node('dependencies/sentry-native-0.4.10')
    if sentry_dir.exists():
        sentry_dir.delete()

def configure(conf):
    #waf configure --check-cxx-compiler=clang++
    conf.load('compiler_cxx')
    conf.load('clang_compilation_database')
    conf.env.CPPFLAGS = ['-g','-std=c++17']
    conf.env.LDFLAGS = ['-Wl,--build-id=sha1']
    conf.env.SENTRY = conf.options.sentry
    conf.env.YAML = conf.options.yaml
    
def build_deps(conf):
    install_dir = conf.path.make_node(deps_install)
    if not install_dir.exists():
        install_dir.mkdir()
    build_dir = conf.path.make_node(yaml_out)
    if not build_dir.exists():
        print('Build yaml-cpp dependency...')
        build_dir.mkdir()
        print('Run cmake...')
        #ret = conf.exec_command('cmake --version')
        ret = conf.exec_command(
            'cmake -S dependencies/yaml-cpp -B {} -DYAML_CPP_BUILD_TESTS=OFF'.format(build_dir.abspath())
        )
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
    sentry_dir = conf.path.make_node('dependencies/sentry-native-0.4.10') 
    if not sentry_dir.exists():
        print('Download sentry ...')
        ret = conf.exec_command(
            ['curl','-Lo','dependencies/sentry.tar.gz',
            'https://github.com/getsentry/sentry-native/archive/refs/tags/0.4.10.tar.gz'],
        )
        if ret: conf.fatal('curl error %i'%ret)
        print('Extract sentry ...')
        ret = conf.exec_command(['tar','-zxvf','dependencies/sentry.tar.gz','-C','dependencies'])
        if ret: conf.fatal('tar error %i'%ret)
        print('Download breakpad ...')
        ret = conf.exec_command(
            ['git','clone','https://chromium.googlesource.com/breakpad/breakpad.git','dependencies/breakpad'])
        if ret: conf.fatal('git error %i'%ret)
        print('Copy breakpad to sentry ...')
        ret = conf.exec_command(['cp','-r','dependencies/breakpad/src/','dependencies/sentry-native-0.4.10/external/breakpad'])
        if ret: conf.fatal('cp error %i'%ret)
        print('Download lss ...')
        ret = conf.exec_command(['git','clone','https://chromium.googlesource.com/linux-syscall-support','dependencies/sentry-native-0.4.10/external/breakpad/src/third_party/lss'])
        if ret: conf.fatal('git error %i'%ret)
        print('Clean unused files ...')
        conf.path.find_node('dependencies/sentry.tar.gz').delete()
        conf.path.find_node('dependencies/breakpad').delete()
    build_dir = conf.path.make_node(out+'/sentry')
    if not build_dir.exists():
        build_dir.mkdir()
        print('Run cmake...')
        ret = conf.exec_command('cmake -B {} -Sdependencies/sentry-native-0.4.10'.format(build_dir.abspath()))
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
# cp sysroot/fedora-arm64/usr/lib64/clang/12.0.1/lib/libclang_rt.* /usr/lib64/clang/12.0.1/lib/linux/
# cp sysroot/fedora-arm32/usr/lib/clang/12.0.1/lib/libclang_rt.* /usr/lib64/clang/12.0.1/lib/linux/
# clang++ --target=arm-linux-gnueabihf --rtlib=compiler-rt --sysroot=sysroot/fedora-arm32 -fuse-ld=lld hello.cpp -o hello
# clang++ --target=aarch64-linux-gnu --rtlib=compiler-rt --sysroot=sysroot/fedora-arm64 -fuse-ld=lld hello.cpp -o hello