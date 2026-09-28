package fr.cerostudio.core;

import fr.cerostudio.api.CeroApi;
import fr.cerostudio.api.mod.ClientModInitializer;
import fr.cerostudio.core.launcher.LauncherArgs;
import fr.cerostudio.core.launcher.LauncherConnection;

import java.util.logging.Logger;

public final class CeroClientMod implements ClientModInitializer {

    private static final Logger LOGGER = Logger.getLogger("CeroClientMod");

    private volatile LauncherConnection launcherConnection;

    @Override
    public void onInitializeClient() {
        String version = CeroApi.minecraftVersion();
        String pseudo = CeroApi.player() != null ? CeroApi.player().getUsername() : "Player";
        CeroApi.window().setTitle("CeroClient - " + version + " - " + pseudo);

        connectToLauncher();
    }

    private void connectToLauncher() {
        String[] programArgs = CeroApi.launchArguments();
        Integer port = LauncherArgs.resolvePort(programArgs);

        if (port == null) {
            LOGGER.warning("Aucun port de launcher fourni, connexion ignorée.");
            return;
        }

        final LauncherConnection connection = new LauncherConnection(port);
        if (!connection.connect()) {
            LOGGER.warning("Échec de connexion au launcher sur le port " + port);
            return;
        }

        // Messages entrants du launcher (NOTIFY;..., PING;..., etc.)
        connection.setMessageHandler(new LauncherConnection.MessageHandler() {
            @Override
            public void onMessage(String message) {
                LOGGER.info("Message du launcher : " + message);
                // TODO: dispatch selon le préfixe (notifications in-game, etc.)
            }
        });

        // Fin de partie (exit normal, SIGTERM, crash non fatal) :
        // on révèle la fenêtre du launcher AVANT de couper la socket.
        Runtime.getRuntime().addShutdownHook(new Thread(new Runnable() {
            @Override
            public void run() {
                connection.send("SHOW");
                connection.disconnect();
            }
        }, "Cero-Bridge-Shutdown"));

        connection.send("HELLO;CeroClient;" + CeroApi.minecraftVersion());

        CeroApi.services().register(LauncherConnection.class, connection);
        this.launcherConnection = connection;
    }

    public LauncherConnection getLauncherConnection() {
        return launcherConnection;
    }
}