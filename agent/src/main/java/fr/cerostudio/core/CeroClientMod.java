package fr.cerostudio.core;

import fr.cerostudio.api.CeroApi;
import fr.cerostudio.api.event.client.GameStopEvent;
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
        registerShutdownHook();
    }

    private void connectToLauncher() {
        String[] programArgs = CeroApi.launchArguments();
        Integer port = LauncherArgs.resolvePort(programArgs);

        if (port == null) {
            LOGGER.warning("Aucun port de launcher fourni, connexion ignorée.");
            return;
        }

        LauncherConnection connection = new LauncherConnection(port);
        if (!connection.connect()) {
            LOGGER.warning("Échec de connexion au launcher sur le port " + port);
            return;
        }

        CeroApi.launcher().bind(port, connection);

        connection.send("HELLO;CeroClient;" + CeroApi.minecraftVersion());

        CeroApi.services().register(LauncherConnection.class, connection);
        this.launcherConnection = connection;
    }

    private void registerShutdownHook() {
        Runtime.getRuntime().addShutdownHook(new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    CeroApi.events().post(new GameStopEvent(System.currentTimeMillis()));
                } catch (Throwable t) {
                    LOGGER.warning("GameStopEvent en échec : " + t);
                }
                LauncherConnection connection = launcherConnection;
                if (connection != null) {
                    try {
                        connection.send("SHOW");
                    } catch (Throwable t) {
                        LOGGER.warning("SHOW en échec : " + t);
                    }
                    try {
                        connection.disconnect();
                    } catch (Throwable t) {
                        LOGGER.warning("Déconnexion en échec : " + t);
                    }
                }
                try {
                    CeroApi.scheduler().shutdown();
                } catch (Throwable t) {
                    LOGGER.warning("Arrêt du scheduler en échec : " + t);
                }
            }
        }, "Cero-Shutdown"));
    }

    public LauncherConnection getLauncherConnection() {
        return launcherConnection;
    }
}