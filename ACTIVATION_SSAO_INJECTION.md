# 🔧 Activer l'Injection SSAO Native

## Étape 1 : Ouvre le menu MariusFX in-game

Appuie sur `Home` (ou ton hotkey).

## Étape 2 : Va dans l'onglet "Add-ons"

Tu devrais voir une section **"MariusFX Pipeline Injection"** (ou similaire).

## Étape 3 : Active "SSAO Injection"

Toggle le switch à `ON`.

## Étape 4 : Désactive les SSAO post-process

Dans l'onglet "Shaders", **désactive** :
- `MartysMods_MXAO`
- `PPFXSSDO`
- `MiAO`
- `Barbatos_XeGTAO`
- `NeoSSAO`
- `MariusAO`

## Étape 5 : Teste

Regarde une flamme, de la fumée, des particules → **l'AO ne doit plus passer à travers**.

---

## ⚠️ Si le toggle n'existe pas dans le menu

C'est que le runtime GUI n'expose pas encore le flag. Dans ce cas, **méthode alternative** :

### Option A : Via le log (temporaire)

Ouvre la console F8 de FiveM, tape :
```
reshade.set_technique_state("SSAO_Injection", true)
```

### Option B : Hardcode (pour test rapide)

Je modifie le code pour forcer l'activation au démarrage.
