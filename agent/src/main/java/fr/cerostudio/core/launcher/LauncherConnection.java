package fr.cerostudio.core.launcher;

import java.io.*;
import java.net.Socket;
import java.util.logging.Level;
import java.util.logging.Logger;

public final class LauncherConnection {

    private static final Logger LOGGER = Logger.getLogger("CeroClient-Launcher");
    private static final String LOCALHOST = "127.0.0.1";
    private static final int CONNECT_TIMEOUT_MS = 3000;

    private final int port;
    private Socket socket;
    private PrintWriter writer;
    private BufferedReader reader;
    private boolean connected = false;

    public LauncherConnection(int port) {
        this.port = port;
    }

    public boolean connect() {
        try {
            socket = new Socket();
            socket.connect(new java.net.InetSocketAddress(LOCALHOST, port), CONNECT_TIMEOUT_MS);
            writer = new PrintWriter(socket.getOutputStream(), true);
            reader = new BufferedReader(new InputStreamReader(socket.getInputStream()));
            connected = true;
            LOGGER.info("Connecté au launcher sur le port " + port);
            return true;
        } catch (IOException e) {
            LOGGER.log(Level.WARNING, "Impossible de se connecter au launcher sur le port " + port, e);
            connected = false;
            return false;
        }
    }

    public void send(String message) {
        if (!connected || writer == null) {
            LOGGER.warning("Tentative d'envoi alors que non connecté au launcher");
            return;
        }
        writer.println(message);
    }

    public String readLine() {
        if (!connected || reader == null) {
            return null;
        }
        try {
            return reader.readLine();
        } catch (IOException e) {
            LOGGER.log(Level.WARNING, "Erreur de lecture depuis le launcher", e);
            return null;
        }
    }

    public boolean isConnected() {
        return connected && socket != null && socket.isConnected() && !socket.isClosed();
    }

    public void disconnect() {
        connected = false;
        try {
            if (writer != null) writer.close();
            if (reader != null) reader.close();
            if (socket != null) socket.close();
        } catch (IOException e) {
            LOGGER.log(Level.WARNING, "Erreur lors de la fermeture de la connexion launcher", e);
        }
    }
}