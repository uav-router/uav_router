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
    conf.load('compiler_cxx')

def build(bld):
    bld.program(
        source=[
            'tests/test.cpp',
            'src/udp.cpp',
            'src/tcp.cpp',
            'src/timer.cpp',
            'src/err.cpp',
            'src/epoll.cpp',
            'src/log.cpp',
            'src/addrinfo.cpp'
        ], 
        target='test',
        includes     = ['src'],
        lib          = ['anl']
    )
