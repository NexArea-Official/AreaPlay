Area Play - plugin areaplay_return.suprx
========================================

A quoi il sert
--------------
Quand tu fermes un jeu lance depuis Area Play, la console revient dans Area Play au lieu de rester
sur la page LiveArea du jeu. Il faut aussi activer, dans Area Play :
Reglages > Systeme > Retour dans Area Play apres un jeu.

Securite : maintenir L pendant le demarrage de la console = le plugin n'est pas charge.

Installation
------------
1. Copie areaplay_return.suprx dans ux0:tai/ (avec VitaShell ; cree le dossier ux0:tai s'il n'existe pas).
2. Ouvre TON fichier config.txt actuel (ur0:tai/config.txt le plus souvent, sinon ux0:tai/config.txt)
   et ajoute la ligne du plugin sous la section *main (cree la section si elle n'existe pas) :

     *main
     ux0:tai/areaplay_return.suprx

3. Redemarre la console (pas seulement la LiveArea).

ATTENTION : taiHEN ne lit qu'UN seul config.txt. Si ux0:tai/config.txt existe, il est lu a la place de
ur0:tai/config.txt. Si ta config est dans ur0:tai, ne cree PAS de config.txt dans ux0:tai (tous tes autres
plugins ne seraient plus charges) : mets seulement le fichier .suprx dans ux0:tai.

Le fichier du plugin est sur ux0 (carte memoire / SD2Vita) : en cas de souci, retirer la carte suffit pour
demarrer sans le plugin (taiHEN passe un plugin introuvable).

Est-ce risque ?
---------------
C'est un plugin "utilisateur", pas un plugin noyau : il ne modifie aucun fichier du systeme, il ne fait que
regarder quelles applis sont ouvertes et relancer Area Play. Il attend 20 s apres le demarrage de la
console avant de faire quoi que ce soit. Reglage desactive dans Area Play : il ne fait rien.
Pire cas (plugin defectueux) : la LiveArea redemarre en boucle. Il suffit alors de retirer la carte
memoire / SD2Vita (le plugin n'est plus charge), puis d'enlever la ligne de ux0:tai/config.txt.

Desinstaller
------------
Enleve la ligne ux0:tai/areaplay_return.suprx de ux0:tai/config.txt, redemarre, puis supprime le fichier.

Journal
-------
ux0:data/AreaPlay/return_log.txt : "pret", "jeu ouvert", "jeu ferme : retour dans Area Play"...
Utile si le retour ne se fait pas.

Compilation
-----------
cmake -S . -B build -G Ninja && cmake --build build   (VITASDK defini) -> build/areaplay_return.suprx
