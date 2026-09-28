package fr.cerostudio.api.launcher;

import fr.cerostudio.core.launcher.LauncherConnection;

import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.function.Consumer;
import java.util.logging.Level;
import java.util.logging.Logger;

public final class LauncherApi {

    private static final Logger LOGGER = Logger.getLogger("CeroApi-Launcher");

    private final List<Consumer<String>> listeners = new CopyOnWriteArrayList<>();
    private volatile LauncherConnection connection;
    private volatile int port = -1;

    public LauncherApi() {}

    public boolean isConnected() {
        LauncherConnection current = connection;
        return current != null && current.isConnected();
    }

    public int port() {
        return port;
    }

    public void send(String message) {
        LauncherConnection current = connection;
        if (current == null || !current.isConnected()) {
            LOGGER.warning("LauncherApi.send() ignoré : bridge launcher non connecté");
            return;
        }
        current.send(message);
    }

    public void send(String action, String... parts) {
        if (action == null || action.isEmpty()) {
            throw new IllegalArgumentException("action ne peut pas être ni null ni vide");
        }
        StringBuilder line = new StringBuilder(action);
        if (parts != null) {
            for (String part : parts) {
                if (part == null || part.isEmpty()) {
                    continue;
                }
                line.append(';').append(part);
            }
        }
        send(line.toString());
    }

    public void onMessage(Consumer<String> listener) {
        if (listener == null) {
            throw new IllegalArgumentException("listener ne peut pas être null");
        }
        listeners.add(listener);
    }

    public void bind(int port, LauncherConnection connection) {
        this.port = port;
        this.connection = connection;
        if (connection != null) {
            connection.setMessageHandler(new LauncherConnection.MessageHandler() {
                @Override
                public void onMessage(String message) {
                    dispatch(message);
                }
            });
        }
    }

    private void dispatch(String message) {
        if (listeners.isEmpty()) {
            LOGGER.info("Message du launcher : " + message);
            return;
        }
        for (Consumer<String> listener : listeners) {
            try {
                listener.accept(message);
            } catch (Exception e) {
                LOGGER.log(Level.WARNING, "Listener launcher a levé une exception", e);
            }
        }
    }
}