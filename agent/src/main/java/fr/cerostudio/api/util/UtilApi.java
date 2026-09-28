package fr.cerostudio.api.util;

import java.awt.Desktop;
import java.awt.Toolkit;
import java.awt.datatransfer.Clipboard;
import java.awt.datatransfer.DataFlavor;
import java.awt.datatransfer.StringSelection;
import java.awt.datatransfer.Transferable;
import java.net.URI;

public final class UtilApi {

    public UtilApi() {}

    public boolean openUrl(String url) {
        if (url == null || url.isEmpty()) {
            throw new IllegalArgumentException("url ne peut pas être ni null ni vide");
        }
        String trimmed = url.trim();
        URI uri;
        try {
            uri = new URI(trimmed);
        } catch (Exception e) {
            return false;
        }
        String scheme = uri.getScheme();
        if (!"http".equalsIgnoreCase(scheme) && !"https".equalsIgnoreCase(scheme)) {
            return false;
        }
        try {
            if (Desktop.isDesktopSupported()
                    && Desktop.getDesktop().isSupported(Desktop.Action.BROWSE)) {
                Desktop.getDesktop().browse(uri);
                return true;
            }
        } catch (Exception ignored) {
        }
        return openUrlFallback(trimmed);
    }

    private boolean openUrlFallback(String url) {
        String os = System.getProperty("os.name", "").toLowerCase();
        String[] command;
        if (os.contains("win")) {
            command = new String[] { "rundll32", "url.dll,FileProtocolHandler", url };
        } else if (os.contains("mac")) {
            command = new String[] { "open", url };
        } else {
            command = new String[] { "xdg-open", url };
        }
        try {
            Runtime.getRuntime().exec(command);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    public String clipboardGet() {
        try {
            Clipboard clipboard = Toolkit.getDefaultToolkit().getSystemClipboard();
            Transferable contents = clipboard.getContents(null);
            if (contents != null && contents.isDataFlavorSupported(DataFlavor.stringFlavor)) {
                Object data = contents.getTransferData(DataFlavor.stringFlavor);
                if (data instanceof String) {
                    return (String) data;
                }
            }
        } catch (Exception ignored) {
        }
        return null;
    }

    public boolean clipboardSet(String text) {
        if (text == null) {
            throw new IllegalArgumentException("text ne peut pas être null");
        }
        try {
            Clipboard clipboard = Toolkit.getDefaultToolkit().getSystemClipboard();
            clipboard.setContents(new StringSelection(text), null);
            return true;
        } catch (Exception e) {
            return false;
        }
    }
}