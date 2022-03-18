import re
import os

env = Environment(LIBPATH=[],
                  CPPFLAGS = ['-Wall','-ggdb', '-g'])

# import path
env['VER']="0.9.2"

# All purpose template filling routine
def create_version(env, target, source):
    out = open(str(target[0]), "wb")
    out.write("#define VERSION \"" + env['VER'] + "\"\n")
    out.close()

# All purpose template filling routine
def template_fill(env, target, source):
    out = open(str(target[0]), "wb")
    inp = open(str(source[0]), "r")

    for line in inp.readlines():
        line = re.sub('@(.*?)@',
                      lambda x: env[x.group(1)],
                      line)
        out.write(line)
        
    out.close()
    inp.close()

# Posix by default
env['PKGCONFIG'] = "pkg-config"
env['LIBPATH'] = []
env.Append(LIBS = [])
env['ENV']['PATH'] = os.environ.get('PATH')
env['PKG_CONFIG_PATH'] = "/export/home/junk/local.fc5/lib/pkgconfig:/usr/local/lib/pkgconfig"

# Since we don't run configure when doing scons
env.Command("config.h",
            ["SConstruct"],
            create_version)
env.Append(CPPPATH=['#/src'],
           LIBPATH=['#/src'],
           )

env.ParseConfig("${PKGCONFIG} --cflags --libs gtk+-2.0")

SConscript(['src/SConscript',
            'examples/SConscript',
            ],
           exports='env')
