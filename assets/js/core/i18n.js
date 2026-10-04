(function (window) {
    'use strict';

    const Cero = window.Cero = window.Cero || {};

    const DICTS = {
        fr: {
            nav: { play: "Jouer", instances: "Instances", message: "Message", settings: "Paramètre" },
            a11y: { minimize: "Réduire", close: "Fermer" },
            closeMenu: { hide: "Masquer le launcher", quit: "Fermer le launcher" },
            common: { save: "Enregistrer", cancel: "Annuler" },
            settings: {
                title: "Paramètres",
                nav: { game: "Jeu", interface: "Interface", account: "Compte", library: "Bibliothèque", about: "À propos" },
                performance: { title: "Performance" },
                ram: { label: "RAM allouée", desc: "Mémoire maximale pour Minecraft" },
                launch: {
                    title: "Lancement",
                    hide: { label: "Fermer le launcher au lancement", desc: "Masque la fenêtre quand Minecraft démarre" },
                    reopen: { label: "Rouvrir au retour", desc: "Réaffiche le launcher quand Minecraft se ferme" }
                },
                appearance: {
                    title: "Apparence",
                    theme: { label: "Thème", desc: "Couleur principale de l'interface" },
                    lang: { label: "Langue", desc: "Langue de l'interface" }
                },
                account: {
                    active: "Compte actif", connected: "Connecté", actions: "Actions",
                    logout: { label: "Se déconnecter", desc: "Supprime le compte de ce launcher", button: "Déconnexion" }
                },
                library: {
                    skins: "Skins",
                    currentSkin: "Skin actuel",
                    currentSkinDesc: "Skin actuellement appliqué sur votre compte Minecraft",
                    uploadTitle: "Glisse un skin PNG ici",
                    uploadSub: "ou clique pour parcourir — 64×32 ou 64×64",
                    empty: "Aucun skin dans ta bibliothèque. Importe-en un !"
                },
                data: {
                    title: "Données",
                    reset: { label: "Réinitialiser les paramètres", desc: "Remet tous les paramètres par défaut", button: "Réinitialiser" }
                },
                about: { version: "Version du launcher", licenses: "Licences" },
                unsaved: { title: "Modifications non sauvegardées", body: "Il vous reste des modifications non sauvegardées.", discard: "Ignorer les changements" },
                logoutModal: { title: "Se déconnecter", confirm: "Se déconnecter de ce compte ?", warning: "Vous devrez vous reconnecter avec Microsoft pour rejouer." }
            },
            play: {
                version: "Version",
                launch: "Jouer",
                close: "Fermer",
                chooseVersion: "Choisir version",
                search: "Rechercher...",
                friends: "Amis",
                online: "en ligne",
                requests: "Demandes",
                addFriend: "Ajouter un ami",
                filters: { all: "Tout", release: "Release", snapshot: "Snap", beta: "Beta", alpha: "Alpha" }
            },
            instances: { soon: "Instances à venir..." },
            maintenance: { title: "Maintenance", desc: "Serveur indisponible, nouvelle tentative automatique...", chatDisabled: "Le chat est indisponible pendant la maintenance" },
            friends: {
                empty: "Aucun ami pour le moment",
                ingame: "En jeu",
                online: "En ligne",
                offline: "Hors ligne",
                loading: "Chargement...",
                noRequests: "Aucune demande",
                noPendingRequests: "Aucune demande en attente"
            }
        },
        en: {
            nav: { play: "Play", instances: "Instances", message: "Chat", settings: "Settings" },
            a11y: { minimize: "Minimize", close: "Close" },
            closeMenu: { hide: "Hide launcher", quit: "Quit launcher" },
            common: { save: "Save", cancel: "Cancel" },
            settings: {
                title: "Settings",
                nav: { game: "Game", interface: "Interface", account: "Account", library: "Library", about: "About" },
                performance: { title: "Performance" },
                ram: { label: "Allocated RAM", desc: "Maximum memory for Minecraft" },
                launch: {
                    title: "Launch",
                    hide: { label: "Close launcher on start", desc: "Hides the window when Minecraft starts" },
                    reopen: { label: "Reopen on exit", desc: "Shows the launcher again when Minecraft closes" }
                },
                appearance: {
                    title: "Appearance",
                    theme: { label: "Theme", desc: "Main color of the interface" },
                    lang: { label: "Language", desc: "Interface language" }
                },
                account: {
                    active: "Active account", connected: "Connected", actions: "Actions",
                    logout: { label: "Log out", desc: "Removes the account from this launcher", button: "Log out" }
                },
                data: {
                    title: "Data",
                    reset: { label: "Reset settings", desc: "Restores all settings to their defaults", button: "Reset" }
                },
                library: {
                    skins: "Skins",
                    currentSkin: "Current skin",
                    currentSkinDesc: "Skin currently applied to your Minecraft account",
                    uploadTitle: "Drop a PNG skin here",
                    uploadSub: "or click to browse — 64×32 or 64×64",
                    empty: "No skins in your library. Import one!"
                },
                about: { version: "Launcher version", licenses: "Licenses" },
                unsaved: { title: "Unsaved changes", body: "You have unsaved changes.", discard: "Discard changes" },
                logoutModal: { title: "Log out", confirm: "Log out of this account?", warning: "You'll need to sign in with Microsoft again to play." }
            },
            play: {
                version: "Version",
                launch: "Play",
                close: "Close",
                chooseVersion: "Choose version",
                search: "Search...",
                friends: "Friends",
                online: "online",
                requests: "Requests",
                addFriend: "Add a friend",
                filters: { all: "All", release: "Release", snapshot: "Snap", beta: "Beta", alpha: "Alpha" }
            },
            instances: { soon: "Instances coming soon..." },
            maintenance: { title: "Maintenance", desc: "Server unreachable, retrying automatically...", chatDisabled: "Chat is unavailable during maintenance" },
            friends: {
                empty: "No friends yet",
                ingame: "In game",
                online: "Online",
                offline: "Offline",
                loading: "Loading...",
                noRequests: "No requests",
                noPendingRequests: "No pending requests"
            }
        },
        de: {
            nav: { play: "Spielen", instances: "Instanzen", message: "Chat", settings: "Einstellungen" },
            a11y: { minimize: "Minimieren", close: "Schließen" },
            closeMenu: { hide: "Launcher ausblenden", quit: "Launcher schließen" },
            common: { save: "Speichern", cancel: "Abbrechen" },
            settings: {
                title: "Einstellungen",
                nav: { game: "Spiel", interface: "Oberfläche", account: "Konto", library: "Bibliothek", about: "Über" },
                performance: { title: "Leistung" },
                ram: { label: "Zugewiesener RAM", desc: "Maximaler Speicher für Minecraft" },
                launch: {
                    title: "Start",
                    hide: { label: "Launcher beim Start schließen", desc: "Blendet das Fenster aus, wenn Minecraft startet" },
                    reopen: { label: "Beim Beenden erneut öffnen", desc: "Zeigt den Launcher wieder an, wenn Minecraft geschlossen wird" }
                },
                appearance: {
                    title: "Erscheinungsbild",
                    theme: { label: "Design", desc: "Hauptfarbe der Oberfläche" },
                    lang: { label: "Sprache", desc: "Sprache der Oberfläche" }
                },
                account: {
                    active: "Aktives Konto", connected: "Verbunden", actions: "Aktionen",
                    logout: { label: "Abmelden", desc: "Entfernt das Konto von diesem Launcher", button: "Abmelden" }
                },
                data: {
                    title: "Daten",
                    reset: { label: "Einstellungen zurücksetzen", desc: "Setzt alle Einstellungen auf Standard zurück", button: "Zurücksetzen" }
                },
                library: {
                    skins: "Skins",
                    currentSkin: "Aktueller Skin",
                    currentSkinDesc: "Skin, der aktuell auf deinem Minecraft-Konto angewendet ist",
                    uploadTitle: "Ziehe einen PNG-Skin hierher",
                    uploadSub: "oder klicke zum Durchsuchen — 64×32 oder 64×64",
                    empty: "Keine Skins in deiner Bibliothek. Importiere einen!"
                },
                about: { version: "Launcher-Version", licenses: "Lizenzen" },
                unsaved: { title: "Nicht gespeicherte Änderungen", body: "Es gibt nicht gespeicherte Änderungen.", discard: "Änderungen verwerfen" },
                logoutModal: { title: "Abmelden", confirm: "Von diesem Konto abmelden?", warning: "Du musst dich erneut mit Microsoft anmelden, um zu spielen." }
            },
            play: {
                version: "Version",
                launch: "Spielen",
                close: "Schließen",
                chooseVersion: "Version wählen",
                search: "Suchen...",
                friends: "Freunde",
                online: "online",
                requests: "Anfragen",
                addFriend: "Freund hinzufügen",
                filters: { all: "Alle", release: "Release", snapshot: "Snap", beta: "Beta", alpha: "Alpha" }
            },
            instances: { soon: "Instanzen folgen in Kürze..." },
            maintenance: { title: "Wartung", desc: "Server nicht erreichbar, automatischer erneuter Versuch...", chatDisabled: "Der Chat ist während der Wartung nicht verfügbar" },
            friends: {
                empty: "Noch keine Freunde",
                ingame: "Im Spiel",
                online: "Online",
                offline: "Offline",
                loading: "Wird geladen...",
                noRequests: "Keine Anfragen",
                noPendingRequests: "Keine ausstehenden Anfragen"
            }
        },
        es: {
            nav: { play: "Jugar", instances: "Instancias", message: "Chat", settings: "Ajustes" },
            a11y: { minimize: "Minimizar", close: "Cerrar" },
            closeMenu: { hide: "Ocultar el launcher", quit: "Cerrar el launcher" },
            common: { save: "Guardar", cancel: "Cancelar" },
            settings: {
                title: "Ajustes",
                nav: { game: "Juego", interface: "Interfaz", account: "Cuenta", library: "Biblioteca", about: "Acerca de" },
                performance: { title: "Rendimiento" },
                ram: { label: "RAM asignada", desc: "Memoria máxima para Minecraft" },
                launch: {
                    title: "Inicio",
                    hide: { label: "Cerrar el launcher al iniciar", desc: "Oculta la ventana cuando Minecraft se inicia" },
                    reopen: { label: "Reabrir al salir", desc: "Vuelve a mostrar el launcher cuando Minecraft se cierra" }
                },
                appearance: {
                    title: "Apariencia",
                    theme: { label: "Tema", desc: "Color principal de la interfaz" },
                    lang: { label: "Idioma", desc: "Idioma de la interfaz" }
                },
                account: {
                    active: "Cuenta activa", connected: "Conectado", actions: "Acciones",
                    logout: { label: "Cerrar sesión", desc: "Elimina la cuenta de este launcher", button: "Cerrar sesión" }
                },
                data: {
                    title: "Datos",
                    reset: { label: "Restablecer ajustes", desc: "Restaura todos los ajustes a sus valores predeterminados", button: "Restablecer" }
                },
                library: {
                    skins: "Skins",
                    currentSkin: "Skin actual",
                    currentSkinDesc: "Skin actualmente aplicado a tu cuenta de Minecraft",
                    uploadTitle: "Arrastra un skin PNG aquí",
                    uploadSub: "o haz clic para explorar — 64×32 o 64×64",
                    empty: "No hay skins en tu biblioteca. ¡Importa uno!"
                },
                about: { version: "Versión del launcher", licenses: "Licencias" },
                unsaved: { title: "Cambios sin guardar", body: "Tienes cambios sin guardar.", discard: "Descartar cambios" },
                logoutModal: { title: "Cerrar sesión", confirm: "¿Cerrar sesión de esta cuenta?", warning: "Deberás iniciar sesión de nuevo con Microsoft para jugar." }
            },
            play: {
                version: "Versión",
                launch: "Jugar",
                close: "Cerrar",
                chooseVersion: "Elegir versión",
                search: "Buscar...",
                friends: "Amigos",
                online: "en línea",
                requests: "Solicitudes",
                addFriend: "Añadir un amigo",
                filters: { all: "Todo", release: "Release", snapshot: "Snap", beta: "Beta", alpha: "Alpha" }
            },
            instances: { soon: "Instancias próximamente..." },
            maintenance: { title: "Mantenimiento", desc: "Servidor no disponible, reintentando automáticamente...", chatDisabled: "El chat no está disponible durante el mantenimiento" },
            friends: {
                empty: "Todavía no tienes amigos",
                ingame: "En juego",
                online: "En línea",
                offline: "Sin conexión",
                loading: "Cargando...",
                noRequests: "Sin solicitudes",
                noPendingRequests: "Sin solicitudes pendientes"
            }
        }
    };

    const SUPPORTED_LANGS = Object.keys(DICTS);
    const DEFAULT_LANG = 'fr';

    let currentLang = DEFAULT_LANG;
    let dict = DICTS[DEFAULT_LANG];

    function detectBrowserLang() {
        const nav = (navigator.language || navigator.userLanguage || DEFAULT_LANG).slice(0, 2).toLowerCase();
        return SUPPORTED_LANGS.includes(nav) ? nav : DEFAULT_LANG;
    }

    function resolveKey(obj, key) {
        return key.split('.').reduce((o, k) => (o && o[k] !== undefined ? o[k] : undefined), obj);
    }

    function t(key, fallback) {
        const val = resolveKey(dict, key);
        if (typeof val === 'string') return val;
        return fallback !== undefined ? fallback : key;
    }

    function applyTranslations(root) {
        const scope = root || document;

        scope.querySelectorAll('[data-i18n]').forEach((el) => {
            const val = resolveKey(dict, el.getAttribute('data-i18n'));
            if (typeof val === 'string') el.textContent = val;
        });

        scope.querySelectorAll('[data-i18n-placeholder]').forEach((el) => {
            const val = resolveKey(dict, el.getAttribute('data-i18n-placeholder'));
            if (typeof val === 'string') el.setAttribute('placeholder', val);
        });

        scope.querySelectorAll('[data-i18n-title]').forEach((el) => {
            const val = resolveKey(dict, el.getAttribute('data-i18n-title'));
            if (typeof val === 'string') el.setAttribute('title', val);
        });

        scope.querySelectorAll('[data-i18n-aria]').forEach((el) => {
            const val = resolveKey(dict, el.getAttribute('data-i18n-aria'));
            if (typeof val === 'string') el.setAttribute('aria-label', val);
        });

        document.documentElement.setAttribute('lang', currentLang);
        document.dispatchEvent(new CustomEvent('cero:i18n-applied', { detail: { lang: currentLang } }));
    }

    function setLanguage(lang, persist) {
        if (!SUPPORTED_LANGS.includes(lang)) lang = DEFAULT_LANG;

        currentLang = lang;
        dict = DICTS[lang];
        applyTranslations();

        if (persist !== false && window.save_settings) {
            window.save_settings({ lang: lang });
        }

        const select = document.getElementById('langSelect');
        if (select && select.value !== lang) select.value = lang;
    }

    async function init() {
        let lang = DEFAULT_LANG;
        try {
            if (window.get_settings) {
                const s = await window.get_settings();
                if (s && s.lang && SUPPORTED_LANGS.includes(s.lang)) lang = s.lang;
                else lang = detectBrowserLang();
            } else {
                lang = detectBrowserLang();
            }
        } catch (e) {
            lang = detectBrowserLang();
        }
        setLanguage(lang, false);
    }

    Cero.i18n = {
        t,
        setLanguage,
        applyTranslations,
        getLang: () => currentLang,
        supportedLangs: SUPPORTED_LANGS.slice(),
    };

    window.t = t;
    window.setLanguage = function (lang) { setLanguage(lang, true); };

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
})(window);