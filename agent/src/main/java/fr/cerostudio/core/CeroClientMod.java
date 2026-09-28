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

        CeroApi.launcher().bind(port, connection);

        Runtime.getRuntime().addShutdownHook(new Thread(new Runnable() {
            @Override
            public void run() {
                connection.send("SHOW");
                connection.disconnect();
                CeroApi.scheduler().shutdown();
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