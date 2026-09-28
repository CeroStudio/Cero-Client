package fr.cerostudio.api.http;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.Charset;
import java.util.Locale;
import java.util.function.Consumer;

public final class HttpApi {

    private static final String USER_AGENT = "CeroClient";
    private static final int DEFAULT_TIMEOUT_MS = 10000;
    private static final int MAX_BODY_BYTES = 4194304;

    public HttpApi() {}

    public String get(String url) {
        return get(url, DEFAULT_TIMEOUT_MS);
    }

    public String get(String url, int timeoutMs) {
        requireHttpUrl(url);
        requireTimeout(timeoutMs);
        HttpURLConnection connection = null;
        try {
            connection = open(url, timeoutMs);
            int status = connection.getResponseCode();
            if (status < 200 || status >= 300) {
                return null;
            }
            return readBody(connection);
        } catch (IOException e) {
            return null;
        } finally {
            if (connection != null) {
                connection.disconnect();
            }
        }
    }

    public void getAsync(String url, Consumer<String> callback) {
        getAsync(url, DEFAULT_TIMEOUT_MS, callback);
    }

    public void getAsync(final String url, final int timeoutMs, final Consumer<String> callback) {
        requireHttpUrl(url);
        requireTimeout(timeoutMs);
        if (callback == null) {
            throw new IllegalArgumentException("callback null");
        }
        Thread worker = new Thread(new Runnable() {
            @Override
            public void run() {
                String body = null;
                try {
                    body = get(url, timeoutMs);
                } catch (RuntimeException e) {
                    body = null;
                }
                try {
                    callback.accept(body);
                } catch (Throwable t) {
                    System.err.println("[Cero-Http] callback en échec : " + t);
                }
            }
        }, "Cero-Http");
        worker.setDaemon(true);
        worker.start();
    }

    public boolean isHttpUrl(String url) {
        return url != null && (url.startsWith("http://") || url.startsWith("https://"));
    }

    private void requireHttpUrl(String url) {
        if (!isHttpUrl(url)) {
            throw new IllegalArgumentException("URL non http/https : " + url);
        }
    }

    private void requireTimeout(int timeoutMs) {
        if (timeoutMs < 0) {
            throw new IllegalArgumentException("timeoutMs négatif : " + timeoutMs);
        }
    }

    private HttpURLConnection open(String url, int timeoutMs) throws IOException {
        HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
        connection.setRequestMethod("GET");
        connection.setConnectTimeout(timeoutMs);
        connection.setReadTimeout(timeoutMs);
        connection.setInstanceFollowRedirects(true);
        connection.setRequestProperty("User-Agent", USER_AGENT);
        return connection;
    }

    private String readBody(HttpURLConnection connection) throws IOException {
        InputStream stream = connection.getInputStream();
        ByteArrayOutputStream buffer = new ByteArrayOutputStream();
        byte[] chunk = new byte[8192];
        try {
            int read;
            while ((read = stream.read(chunk)) != -1) {
                buffer.write(chunk, 0, read);
                if (buffer.size() > MAX_BODY_BYTES) {
                    return null;
                }
            }
        } finally {
            stream.close();
        }
        return new String(buffer.toByteArray(), Charset.forName(charsetOf(connection.getContentType())));
    }

    private String charsetOf(String contentType) {
        if (contentType == null) {
            return "UTF-8";
        }
        String lower = contentType.toLowerCase(Locale.ROOT);
        int index = lower.indexOf("charset=");
        if (index < 0) {
            return "UTF-8";
        }
        String charset = lower.substring(index + "charset=".length());
        int end = charset.indexOf(';');
        if (end >= 0) {
            charset = charset.substring(0, end);
        }
        charset = charset.trim();
        try {
            Charset.forName(charset);
            return charset;
        } catch (Exception e) {
            return "UTF-8";
        }
    }
}