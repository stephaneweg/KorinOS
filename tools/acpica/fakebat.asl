/*
 * fakebat.asl - une fausse batterie et un faux secteur pour essayer acpid sous QEMU
 * (docs/etude/BATTERIE-PC.md) : compilée par tools/acpica/fakebat.sh (iasl d'ACPICA), donnée à QEMU par
 * « -acpitable file=build/pc/fakebat.aml ».
 *
 * Batterie neuve 90 Wh, 80 Wh à la dernière charge, 11,4 V, 123 cycles ; 21 % au départ, 15 W de
 * décharge, et chaque lecture de _BST retire 1,5 Wh (une décharge accélérée : 20 % à la première
 * lecture, 10 % vers la 7e, 5 % vers la 10e) ; secteur débranché. FAKE = 1 : secteur branché, en charge.
 */
DefinitionBlock ("", "SSDT", 2, "KORIN", "FAKEBAT", 1)
{
    Scope (\_SB)
    {
        Name (FAKE, 0)

        Device (BAT0)
        {
            Name (_HID, EisaId ("PNP0C0A"))
            Name (_UID, One)
            Name (RCAP, 17000)
            Name (BSTP, Package (4) { 1, 15000, 0, 11400 })

            Method (_STA, 0, NotSerialized)
            {
                Return (0x1F)
            }

            Method (_BIX, 0, NotSerialized)
            {
                Return (Package (20)
                {
                    0, 0, 90000, 80000, 1, 11400, 8000, 4000, 123, 95000,
                    0, 0, 0, 0, 100, 100, "FakeBat 80", "0001", "LION", "KORIN"
                })
            }

            Method (_BST, 0, Serialized)
            {
                If (FAKE)
                {
                    BSTP [0] = 2
                    RCAP += 1500
                }
                ElseIf (RCAP > 1500)
                {
                    RCAP -= 1500
                }
                BSTP [2] = RCAP
                Return (BSTP)
            }
        }

        Device (ADP1)
        {
            Name (_HID, "ACPI0003")

            Method (_PSR, 0, NotSerialized)
            {
                Return (FAKE)
            }
        }
    }
}
