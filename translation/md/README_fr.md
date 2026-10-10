<h1 align="center"><img src="../../assets/logo.png" width="30" alt="Icône CeroClient"> <strong>Cero</strong>Client</h1>

<p align="center">
  CeroClient est un client Minecraft gratuit et open source conçu pour être hautement optimisé et léger.
</p>

<p align="center">
  <img src="https://img.shields.io/github/stars/CeroWorks/Cero-Client?style=flat" alt="Stars">
  <img src="https://img.shields.io/github/license/CeroWorks/Cero-Client?style=flat" alt="License">
  <img src="https://img.shields.io/badge/Minecraft-1.7.10--26.3-green" alt="Versions">
  <img src="https://img.shields.io/github/v/release/CeroWorks/Cero-Client?style=flat" alt="Release">
</p>

<p align="center">
  <img src="../../screenshots/launcher1.png" width="800" alt="Lanceur CeroClient">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/C-A8B9CC?style=flat&logo=c&logoColor=black" alt="C">
  <img src="https://img.shields.io/badge/C++-00599C?style=flat&logo=cplusplus&logoColor=white" alt="C++">
  <img src="https://img.shields.io/badge/JavaScript-F7DF1E?style=flat&logo=javascript&logoColor=black" alt="JavaScript">
  <img src="https://img.shields.io/badge/HTML5-E34F26?style=flat&logo=html5&logoColor=white" alt="HTML">
  <img src="https://img.shields.io/badge/CSS3-1572B6?style=flat&logo=css3&logoColor=white" alt="CSS">
  <img src="https://img.shields.io/badge/Go-00ADD8?style=flat&logo=go&logoColor=white" alt="Go">
  <img src="https://img.shields.io/badge/Python-3776AB?style=flat&logo=python&logoColor=white" alt="Python">
  <img src="https://img.shields.io/badge/Java-ED8B00?style=flat&logo=openjdk&logoColor=white" alt="Java">
  <img src="https://img.shields.io/badge/Rust-000000?style=flat&logo=rust&logoColor=white" alt="Rust">
  <img src="https://img.shields.io/badge/Nim-FFE953?style=flat&logo=nim&logoColor=black" alt="Nim">
  <img src="https://img.shields.io/badge/Objective--C++-438EFF?style=flat" alt="Objective-C++">
  <img src="https://img.shields.io/badge/Lua-2C2D72?style=flat&logo=lua&logoColor=white" alt="Lua">
</p>

---
### Traductions

- [English](../../README.md)

## Fonctionnalités & Tâches

- [ ] **Instances**
    - [x] Installer un chargeur (ex. : Forge, Fabric, etc.)
    - [x] Sauvegarder et gérer les instances
    - [ ] Télécharger des mods depuis l'UI
- [x] **Jouer à Minecraft**
    - [x] Télécharger le manifeste
    - [x] Lire les métadonnées
    - [x] Télécharger les bibliothèques
    - [x] Télécharger le client
    - [x] Télécharger les assets
    - [x] Démarrer le client
    - [x] Installer Fabric
    - [x] Installer Forge
    - [x] Installer Quilt
    - [x] Démarrer Fabric
    - [x] Démarrer Forge
    - [x] Démarrer Quilt
- [x] **Connexion au compte Microsoft**
- [x] Créer un installateur
- [x] Créer un système de mise à jour (Updater)
- [x] Réécrire le lanceur en C/C++
- [ ] Système d'amis et de chat
    - [x] Envoyer un message
    - [ ] Inviter dans son monde
    - [x] Ajouter un ami
    - [x] Supprimer un ami
- [ ] Create an Android App
- [x] Support multilingue

---

## Installation

Tous les téléchargements et instructions pour CeroClient sont disponibles sur notre [Site Web](https://cerostudio.fr/ceroclient) (pas encore à jour) ou depuis les releases.

*Note : macOS est largement non testé — nous n'avons actuellement aucun testeur sur macOS.*

## Architecture

| Composant | Langage | Rôle |
|---|---|---|
| `src/`, `include/` | C / C++ | Lanceur natif, interface webview, IPC, chiffrement AES-GCM |
| `agent/` | Java | Service de mixin, remappeur MCP/ProGuard vers Tiny, classloader personnalisé |
| `bootstrapper/` | Rust | Installation et mise à jour automatique |
| `server/` | Go | Authentification, amis, messagerie WebSocket |
| `server/commands/lua/` | Lua | Commandes serveur extensibles (kick, status, friends...) |
| `assets/` | HTML/CSS/JS | Interface utilisateur |
| `tools/`, `build.py` | Python | Pipeline de build, empaquetage d'assets, obfuscation JS |
| `installer/` | Nim / NSIS | Installateurs de plateformes (type Unix / WinNT) |

## Compilation depuis les sources

Si vous souhaitez compiler CeroClient vous-même, vous pouvez utiliser les scripts de build fournis dans le dépôt.

*Pour plus d'aide sur l'installation des prérequis et la compilation, veuillez consulter `BUILDING.md`.*

**Prérequis (Linux) :**
* **GCC / G++** ≥ `13.3.0`
* **Python** ≥ `3.9` (Testé : 3.13)
* **Rust / Cargo** (Dernière version stable)
* **pkg-config**
* **Dépendances :** `gtk+-3.0`, `webkit2gtk-4.1`, `libcurl` (et `ayatana-appindicator3-0.1` ou `appindicator3-0.1` pour le support de la zone de notification / system tray)

**Prérequis (Windows) :**
* **MinGW-w64** (GCC / G++)
* **Rust / Cargo** (Dernière version stable)
* Dépendances pré-compilées dans `%USERPROFILE%\mingw-deps\x64-windows`

### Instructions

1. Clonez le dépôt.
2. Installez toutes les dépendances Python : `pip install -r requirements.txt`
3. Exécutez le script de build : `python3 build.py`

*Note : Sous Windows, WebView2 nécessite une configuration supplémentaire — exécutez `setup_webview.bat` avant de compiler.*

### Développement

Vous pouvez l'exécuter facilement pour le débogage ou pour tester une modification.

1. Installez toutes les dépendances Python : `pip install -r requirements.txt`
2. Exécutez le script d'exécution : `python3 run.py`

*Note : Sous Windows, WebView2 nécessite une configuration supplémentaire — exécutez `setup_webview.bat` avant de compiler.*

---
## Licence

Ce projet est sous licence GNU General Public License v3.0 uniquement.
Voir le fichier [LICENSE](../../LICENSE) pour plus de détails.

CeroClient n'inclut ni ne distribue Minecraft lui-même ou les actifs propriétaires appartenant à Mojang ou Microsoft. Les utilisateurs sont tenus d'obtenir et d'utiliser Minecraft conformément aux conditions de Mojang.

Copyright © 2025–2026 Cero Studio.
