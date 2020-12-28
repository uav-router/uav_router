#! /usr/bin/env python
# encoding: utf-8

VERSION='0.0.1'
APPNAME='ioloop_test'

# these variables are mandatory ('/' are converted automatically)
top = '.'
out = 'build'

def options(opt):
    opt.load('compiler_cxx')

def configure(conf):
    # waf configure --check-cxx-compiler=clang++
    conf.load('compiler_cxx')
    conf.load('clang_compilation_database')
    conf.env.CPPFLAGS = ['-g']
def build(bld):
    sources = bld.path.find_node('src').ant_glob('*.cpp')
    tests_dir = bld.path.find_node('tests')
    bld.program(
        source= [ tests_dir.find_node('test.cpp') ] + sources,
        target='test',
        includes     = ['src'],
        lib          = ['anl']
    )
    bld.program(
        source= [ tests_dir.find_node('tcp_test.cpp') ] + sources,
        target='tcptest',
        includes     = ['src'],
        lib          = ['anl']
    )
    bld.program(
        source= [ tests_dir.find_node('udp_test.cpp') ] + sources,
        target='udptest',
        includes     = ['src'],
        lib          = ['anl']
    )
