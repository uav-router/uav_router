#! /usr/bin/env python
# encoding: utf-8

import os

VERSION='0.0.1'
APPNAME='uav-router'

# these variables are mandatory ('/' are converted automatically)
top = '.'
out = 'build'

def options(opt):
    opt.load('compiler_cxx')

def configure(conf):
    #waf configure --check-cxx-compiler=clang++
    conf.load('compiler_cxx')
    conf.load('clang_compilation_database')
    conf.env.CPPFLAGS = ['-g','-std=c++17']
def build(bld):
    sources = bld.path.find_node('src').ant_glob('*.cpp')
    tests = bld.path.find_node('tests').ant_glob('*.cpp')
    app = bld.path.find_node('app').ant_glob('*.cpp')
    yamllib = bld.path.find_node('dependencies').find_node('yaml-cpp').find_node('build')
    bld.objects(
        source   = sources,
        target   = 'common_objs',
        defines  = ['YAML_CONFIG'],
        includes = ['dependencies/yaml-cpp/include']
    )
    for test in tests:
        bld.program(
            source       = [test],
            use          = 'common_objs',
            target       = os.path.splitext(test.name)[0],
            includes     = ['src','dependencies/yaml-cpp/include'],
            defines      = ['YAML_CONFIG'],
            lib          = ['anl','udev','avahi-common','avahi-client','avahi-core','yaml-cpp'],
            libpath      = [yamllib.abspath()]
        )
    bld.program(
        source       = app,
        use          = 'common_objs',
        target       = 'uav-router',
        includes     = ['src','dependencies/yaml-cpp/include'],
        defines      = ['YAML_CONFIG'],
        lib          = ['anl','udev','avahi-common','avahi-client','avahi-core','yaml-cpp'],
        libpath      = [yamllib.abspath()]
    )