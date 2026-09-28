package fr.cerostudio.api.discord;

import fr.cerostudio.api.launcher.LauncherApi;

public final class DiscordApi {

    private final LauncherApi launcher;

    public DiscordApi(LauncherApi launcher) {
        this.launcher = launcher;
    }

    public boolean isAvailable() {
        return launcher.isConnected();
    }

    public void setPresence(String details, String state) {
        buildAndSend(details, state, null);
    }

    public void setPresence(String details, String state, long startTimestampSeconds) {
        buildAndSend(details, state, Long.toString(startTimestampSeconds));
    }

    public void clearPresence() {
        launcher.send("DISCORD_CLEAR");
    }

    private void buildAndSend(String details, String state, String timestamp) {
        if (details == null || details.isEmpty()) {
            throw new IllegalArgumentException("details ne peut pas être ni null ni vide");
        }
        if (details.indexOf(';') >= 0) {
            throw new IllegalArgumentException("details ne peut pas contenir de ';'");
        }
        if (state != null && state.indexOf(';') >= 0) {
            throw new IllegalArgumentException("state ne peut pas contenir de ';'");
        }

        StringBuilder line = new StringBuilder("DISCORD;");
        line.append(details).append(';');
        if (state != null) {
            line.append(state);
        }
        if (timestamp != null) {
            line.append(';').append(timestamp);
        }
        launcher.send(line.toString());
    }
}