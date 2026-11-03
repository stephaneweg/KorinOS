"""trdiff.py a b : premiere ligne ou deux traces different, sans la colonne C (cycles en decimal au milieu)"""
import sys
a = open(sys.argv[1]).read().split("\n")
b = open(sys.argv[2]).read().split("\n")
for i, (x, y) in enumerate(zip(a, b)):
    if x != y:
        for j in range(max(0, i - 2), i + 2):
            print(j, a[j])
            print(j, b[j])
        break
else:
    print("identiques", len(a), len(b))
