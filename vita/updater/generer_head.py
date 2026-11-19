"""Fabrique updater/sce_sys/package/head.bin, demande par l'installateur du systeme (promotion) quand Area Play
installe Area Play Updater.

Modele : head_template.bin (en-tete de paquet de SKGleba/VitaDeploy, licence MIT : head_template_LICENSE.txt).
On y ecrit l'identifiant de contenu, puis les trois sommes de controle de l'en-tete sont recalculees.
Utilisation : python updater/generer_head.py   (a refaire seulement si le Title ID change)
"""
import hashlib
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
TITLE_ID = "ARPL00002"
CONTENT_ID = f"EP9000-{TITLE_ID}_00-0000000000000000"  # pas de CONTENT_ID dans le param.sfo d'un homebrew


def digest(data):
    """Somme de 16 octets utilisee par les en-tetes de paquet."""
    d = hashlib.sha1(data).digest()
    block = bytearray(64)
    block[0:8] = d[4:12]
    block[8:16] = d[4:12]
    block[16:20] = d[12:16]
    block[20] = d[16]
    block[21:24] = d[1:4]
    block[24:32] = block[16:24]
    return hashlib.sha1(bytes(block)).digest()[:16]


def be32(b, off):
    return struct.unpack(">I", b[off:off + 4])[0]


head = bytearray(open(os.path.join(HERE, "head_template.bin"), "rb").read())
head[0x30:0x30 + 48] = CONTENT_ID.encode().ljust(48, b"\0")[:48]
n = be32(head, 0xD0)                      # en-tete
head[n:n + 16] = digest(bytes(head[:n]))
off, ln, out = be32(head, 0x08), be32(head, 0x10), be32(head, 0xD4)  # bloc d'informations
head[out:out + 16] = digest(bytes(head[off:off + ln - 64]))
n = be32(head, 0xE8)                      # ensemble
head[n:n + 16] = digest(bytes(head[:n]))
dst = os.path.join(HERE, "sce_sys", "package", "head.bin")
os.makedirs(os.path.dirname(dst), exist_ok=True)
open(dst, "wb").write(head)
print(dst, len(head), CONTENT_ID)
