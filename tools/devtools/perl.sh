# perl.sh : Perl 5.40.0 pour Korin PC, par perl-cross 1.6 (lu par tools/devtools/build.sh, pas lancé seul).
# Perl tout statique (les extensions XS dans le binaire, pas de dlopen) dans /devtools/gcc : bin/perl,
# lib/perl5/5.40.0 (les modules ; ceux des générateurs de WebKit : File::*, Getopt::Long, JSON::PP, FindBin,
# Data::Dumper, Digest::*...). Sans NDBM_File ni ODBM_File (le hash de newlib demande _impure_ptr), sans
# l'extension re (son regcomp double celui du cœur en statique).
say perl
fetch perl-5.40.0.tar.gz https://www.cpan.org/src/5.0/perl-5.40.0.tar.gz \
	c740348f357396327a9795d3e8323bafd0fe8a5c7835fc1cbaba0cc8dfe7161f
fetch perl-cross-1.6.tar.gz https://github.com/arsv/perl-cross/releases/download/1.6/perl-cross-1.6.tar.gz \
	5abf198ee50ce9e69eb68fede68c87f65241caa744e4203e97490fa59b45ed69
unpack perl-5.40.0.tar.gz perl-5.40.0
tar -C "$WORK/src" -xf "$DL/perl-cross-1.6.tar.gz"
cp -rf "$WORK/src/perl-cross-1.6/." "$WORK/src/perl-5.40.0/"
rm -rf "$WORK/src/perl-cross-1.6"
cd "$WORK/src/perl-5.40.0"
# Errno.pm : les codes d'erreur de newlib et de libposix (le miniperl de l'hôte se croit sous Linux et lirait
# /usr/include/errno.h)
python3 - <<'EOF'
p = 'ext/Errno/Errno_pm.PL'
s = open(p).read()
s = s.replace("sub get_files {\n    my @file;",
              "sub get_files {\n    my @file;\n    return split / /, $ENV{KORIN_ERRNO_H} if $ENV{KORIN_ERRNO_H};", 1)
open(p, 'w').write(s)
EOF
export KORIN_ERRNO_H="$TOOL/$TARGET/include/sys/errno.h $ROOT/build/pc/posix/include/errno.h"
./configure --prefix=$PREFIX --target=$TARGET --with-cc="$CC" --with-ranlib=${TC}ranlib --all-static \
	-Dusedl=undef -Dd_dlopen=undef -Dusethreads=undef -Doptimize="-O2 -g0" -Dosname=korin -Dd_nanosleep=define \
	-Dd_ctermid=undef -Accflags=-D_GNU_SOURCE --disable-mod=ext/NDBM_File,ext/ODBM_File,ext/re \
	>configure.log 2>&1 || { tail -30 configure.log; die "perl : configure"; }
make -j"$JOBS" >make.log 2>&1 || { grep -n 'error\|undefined reference' make.log | head -40; die "perl : make"; }
# (les .pm des extensions statiques, File::Spec, List::Util... : leur pm_to_blib est marqué fait sans que les
# fichiers soient dans lib/ ; refait pour chacune)
for mf in cpan/*/Makefile dist/*/Makefile ext/*/Makefile; do
	d=$(dirname "$mf")
	rm -f "$d/pm_to_blib"
	make -C "$d" PERL_CORE=1 pm_to_blib >>make.log 2>&1 || die "perl : pm_to_blib $d"
done
tree perl
make DESTDIR="$T" install >install.log 2>&1 || { tail -20 install.log; die "perl : install"; }
${TC}strip "$T$PREFIX/bin/perl"
rm -rf "$T$PREFIX/share/man" "$T$PREFIX/lib/perl5/5.40.0/pod"
find "$T$PREFIX/lib/perl5" -name '*.a' -delete
# (les extensions statiques sont dans le binaire : les archives et les en-têtes de CORE ne servent qu'à lier)
