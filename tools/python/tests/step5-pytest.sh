#!/bin/bash
# Essais pytest de l'etape 5 (docs/PYTHON-PORT.md, « Étape 5 ») sur la carte d'essai :
#     bash /home/pytest/step5-pytest.sh rpds|pydantic|cryptography [options de pytest de plus]
# Les dossiers tests viennent des sdist (tools/python/site-packages.sh, dans /home/pytest/rust-ext) ;
# pytest tel quel depuis l'arbre POSIX des chemins (etape 6 : getcwd rend /sd0/...). Resultat complet dans /home/pytest/pytest-<nom>.txt (sur la carte : survit a RAM0).
R=/home/pytest/rust-ext
P="python3 -m pytest -q -p no:cacheprovider -rfE"
what=$1; shift
case $what in
rpds)
	cd $R/rpds_py-0.30.0 && $P tests "$@" ;;
pydantic)
	# sans pytest-benchmark (bancs), pytest-examples (test_docstrings), pytest-run-parallel (marque
	# thread_unsafe) ; delai de pytest-timeout releve pour QEMU
	cd $R/pydantic_core-2.46.4 && $P -o addopts= -o timeout=900 \
		-W ignore::pytest.PytestUnknownMarkWarning \
		--ignore=tests/benchmarks --ignore=tests/test_docstrings.py tests "$@" ;;
cryptography)
	cd $R/cryptography-50.0.0 && $P -o "addopts=-r s --strict-markers" "$@" ;;
*)
	echo "rpds|pydantic|cryptography" >&2; exit 2 ;;
esac > /home/pytest/pytest-$what.txt 2>&1
rc=$?
grep -E '^(FAILED|ERROR) |passed|failed|error' /home/pytest/pytest-$what.txt | cut -c1-200 | tail -n 80
exit $rc
