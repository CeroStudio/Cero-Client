package fr.cerostudio.core;

import fr.cerostudio.api.CeroApi;
import fr.cerostudio.api.mod.ClientModInitializer;
import fr.cerostudio.core.launcher.LauncherArgs;
import fr.cerostudio.core.launcher.LauncherConnection;

import java.util.logging.Logger;

public final class CeroClientMod implements ClientModInitializer {

    private static final Logger LOGGER = Logger.getLogger("CeroClientMod");

    private LauncherConnection launcherConnection;

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

        launcherConnection = new LauncherConnection(port);
        boolean success = launcherConnection.connect();

        if (success) {
            launcherConnection.send("HELLO;CeroClient;" + CeroApi.minecraftVersion());
        } else {
            LOGGER.warning("Échec de connexion au launcher sur le port " + port);
        }
    }

    public LauncherConnection getLauncherConnection() {
        return launcherConnection;
    }
}