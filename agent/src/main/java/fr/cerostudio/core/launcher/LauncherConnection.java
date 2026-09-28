package fr.cerostudio.core.launcher;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.logging.Level;
import java.util.logging.Logger;

public final class LauncherConnection {

    private static final Logger LOGGER = Logger.getLogger("CeroClient-Launcher");
    private static final String LOCALHOST = "127.0.0.1";
    private static final int CONNECT_TIMEOUT_MS = 3000;

    public interface MessageHandler {
        void onMessage(String message);
    }

    private final int port;
    private Socket socket;
    private PrintWriter writer;
    private BufferedReader reader;
    private volatile boolean connected = false;
    private volatile MessageHandler messageHandler;
    private Thread readerThread;

    public LauncherConnection(int port) {
        this.port = port;
    }

    public boolean connect() {
        try {
            socket = new Socket();
            socket.connect(new InetSocketAddress(LOCALHOST, port), CONNECT_TIMEOUT_MS);
            socket.setTcpNoDelay(true);
            writer = new PrintWriter(new OutputStreamWriter(
                    socket.getOutputStream(), StandardCharsets.UTF_8), true);
            reader = new BufferedReader(new InputStreamReader(
                    socket.getInputStream(), StandardCharsets.UTF_8));
            connected = true;

            readerThread = new Thread(new Runnable() {
                @Override
                public void run() {
                    readLoop();
                }
            }, "Cero-Launcher-Bridge");
            readerThread.setDaemon(true);
            readerThread.start();

            LOGGER.info("Connecté au launcher sur le port " + port);
            return true;
        } catch (IOException e) {
            LOGGER.log(Level.WARNING, "Impossible de se connecter au launcher sur le port " + port, e);
            connected = false;
            closeQuietly();
            return false;
        }
    }

    private void readLoop() {
        try {
            String line;
            while (connected && (line = reader.readLine()) != null) {
                if (line.isEmpty()) continue;
                MessageHandler handler = messageHandler;
                if (handler == null) continue;
                try {
                    handler.onMessage(line);
                } catch (Exception e) {
                    LOGGER.log(Level.WARNING, "Handler de message a levé une exception", e);
                }
            }
        } catch (IOException e) {
            if (connected) {
                LOGGER.log(Level.WARNING, "Connexion au launcher interrompue", e);
            }
        } finally {
            connected = false;
            LOGGER.info("Bridge launcher déconnecté");
        }
    }

    public void send(String message) {
        if (!isConnected() || writer == null) {
            LOGGER.warning("Tentative d'envoi alors que non connecté au launcher");
            return;
        }
        writer.println(message);
        if (writer.checkError()) {
            connected = false;
            LOGGER.warning("Envoi vers le launcher échoué (connexion morte ?)");
        }
    }

    public void setMessageHandler(MessageHandler handler) {
        this.messageHandler = handler;
    }

    public boolean isConnected() {
        return connected && socket != null && socket.isConnected() && !socket.isClosed();
    }

    public void disconnect() {
        connected = false;
        closeQuietly();
    }

    private void closeQuietly() {
        try {
            if (writer != null) writer.close();
        } catch (Exception ignored) {
        }
        try {
            if (reader != null) reader.close();
        } catch (Exception ignored) {
        }
        try {
            if (socket != null) socket.close();
        } catch (IOException e) {
            LOGGER.log(Level.WARNING, "Erreur lors de la fermeture de la connexion launcher", e);
        }
    }
}