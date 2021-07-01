#! /usr/bin/env python
# encoding: utf-8

import os
from waflib.Scripting import distclean as original_distclean

VERSION='0.0.1'
APPNAME='uav-router'

# these variables are mandatory ('/' are converted automatically)
top = '.'
out = 'build'

def bool_opt(opt):
    return opt.upper()=="TRUE"

def options(opt):
    opt.add_option('--sentry', action='store', default="no", type="choice", choices=["yes","no"], help='sentry integration')
    opt.add_option('--yaml', action='store', default="yes", type="choice", choices=["yes","no"], help='with yaml configuration')
    opt.load('compiler_cxx')

def distclean(ctx):
    original_distclean(ctx)
    build_dir = ctx.path.make_node('dependencies/yaml-cpp/build')
    if build_dir.exists():
        build_dir.delete()

def configure(conf):
    #waf configure --check-cxx-compiler=clang++
    conf.load('compiler_cxx')
    conf.load('clang_compilation_database')
    conf.env.CPPFLAGS = ['-g','-std=c++17']
    conf.env.SENTRY = conf.options.sentry
    conf.env.YAML = conf.options.yaml
    if conf.env.YAML=='yes':
        build_dir = conf.path.make_node('dependencies/yaml-cpp/build')
        if not build_dir.exists():
            print('Build yaml-cpp dependency...')
            build_dir.mkdir()
            print('Run cmake...')
            conf.exec_command('cmake -Sdependencies/yaml-cpp -Bdependencies/yaml-cpp/build -DYAML_CPP_BUILD_TESTS=OFF')
            print('Run make...')
            conf.exec_command('make -C dependencies/yaml-cpp/build')
            print('Build yaml-cpp complete')
def build(bld):
    print('sentry\t- %r' % bld.env.SENTRY)
    print('yaml\t- %r' % bld.env.YAML)
    base_src = [ bld.path.find_node('src/err.cpp'),
                bld.path.find_node('src/log.cpp')]
    io_src =  [src for src in bld.path.find_node('src').ant_glob('*.cpp') if src not in base_src]
    tests = bld.path.find_node('tests').ant_glob('*.cpp')
    app = bld.path.find_node('app').ant_glob('*.cpp')
    
    libs = ['anl','udev','avahi-common','avahi-client','avahi-core']
    defs = []
    libpath = []
    incs = []
    if bld.env.YAML=='yes':
        yamllib = bld.path.find_node('dependencies').find_node('yaml-cpp').find_node('build')
        libs.append('yaml-cpp')
        defs.append('YAML_CONFIG')
        libpath.append(yamllib.abspath())
        incs.append('dependencies/yaml-cpp/include')
    
    bld.stlib(
        source   = base_src,
        target   = 'uavr-base',
        defines  = defs,
        includes = incs
    )
    bld.stlib(
        source   = io_src,
        target   = 'uavr-io',
        defines  = defs,
        includes = incs
    )
    incs.append('src')
    for test in tests:
        bld.program(
            source       = [test],
            use          = ['uavr-base','uavr-io'],
            target       = os.path.splitext(test.name)[0],
            includes     = incs,
            defines      = defs,
            lib          = libs,
            libpath      = libpath
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
    else:
        print('uav-router is built with yamp-cpp dependency only')