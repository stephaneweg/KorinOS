# ruby.sh : Ruby 3.3.6 pour Korin PC (lu par tools/devtools/build.sh, pas lancé seul). Tout statique (les
# extensions dans le binaire, pas de dlopen), /devtools/gcc : bin/ruby, lib/ruby/3.3.0 (la bibliothèque
# standard). Ce que l'offlineasm de JavaScriptCore (le LLInt) et les générateurs de WebKit demandent : digest,
# json, date, stringio, pathname, set, optparse, shellwords... Sans les extensions qui demandent une
# bibliothèque absente du sysroot du PC (openssl, psych/libyaml, fiddle/libffi, readline, dbm, gdbm) ; sans
# RubyGems (rien à installer par le réseau ici). dev_t et gid_t de newlib font 16 bits : SHORT2NUM, que Ruby
# n'a pas, est INT2FIX. baseruby : le Ruby 3.3 de l'hôte (même version).
say ruby
fetch ruby-3.3.6.tar.gz https://cache.ruby-lang.org/pub/ruby/3.3/ruby-3.3.6.tar.gz \
	8dc48fffaf270f86f1019053f28e51e4da4cce32a36760a0603a9aee67d7fd8d
command -v ruby >/dev/null || die "ruby : il faut un Ruby 3.3 sur l'hôte (baseruby)"
unpack ruby-3.3.6.tar.gz ruby-3.3.6
# (pas de gemmes livrées avec Ruby : leurs extensions se construiraient en .so)
rm -rf "$WORK/src/ruby-3.3.6/.bundle" "$WORK/src/ruby-3.3.6/gems/"*.gem
: > "$WORK/src/ruby-3.3.6/gems/bundled_gems"
cat > "$WORK/src/ruby-3.3.6/ext/Setup" <<'EOF'
option nodynamic
cgi/escape
continuation
coverage
date
digest
digest/bubblebabble
digest/md5
digest/rmd160
digest/sha1
digest/sha2
erb/escape
etc
fcntl
io/console
io/nonblock
io/wait
json
json/generator
json/parser
monitor
objspace
pathname
racc/cparse
rbconfig/sizeof
ripper
stringio
strscan
EOF
export ac_cv_func_getpgrp_void=yes ac_cv_func_setpgrp_void=yes rb_cv_negative_time_t=yes
export ac_cv_func_dl_iterate_phdr=no ac_cv_header_ucontext_h=no rb_cv_stack_end_address=no
gnu ruby-3.3.6 --with-baseruby="$(command -v ruby)" --disable-shared --enable-static --with-static-linked-ext \
	--disable-install-doc --disable-install-rdoc --disable-rubygems --disable-jit-support --disable-yjit \
	--without-gmp --with-coroutine=amd64 CPPFLAGS="-DSHORT2NUM=INT2FIX -DUSHORT2NUM=INT2FIX" --with-out-ext=openssl,psych,fiddle,readline,dbm,gdbm,socket,pty,zlib,win32,win32ole,syslog,nkf,bigdecimal,-test-/*
tree ruby
make DESTDIR="$T" install-nodoc >install.log 2>&1 || { tail -20 install.log; die "ruby : install"; }
${TC}strip "$T$PREFIX/bin/ruby"
rm -rf "$T$PREFIX/share/man" "$T$PREFIX/include" "$T$PREFIX/lib/pkgconfig"
find "$T$PREFIX/lib" -name '*.a' -delete
