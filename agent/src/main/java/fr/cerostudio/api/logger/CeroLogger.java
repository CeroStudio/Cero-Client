package fr.cerostudio.api.logger;

public final class CeroLogger {

    private final String modId;
    private final LoggerApi owner;

    CeroLogger(String modId, LoggerApi owner) {
        this.modId = modId;
        this.owner = owner;
    }

    public String modId() {
        return modId;
    }

    public void debug(String message) {
        log(LogLevel.DEBUG, message, null);
    }

    public void info(String message) {
        log(LogLevel.INFO, message, null);
    }

    public void warn(String message) {
        log(LogLevel.WARN, message, null);
    }

    public void error(String message) {
        log(LogLevel.ERROR, message, null);
    }

    public void error(String message, Throwable throwable) {
        log(LogLevel.ERROR, message, throwable);
    }

    private void log(LogLevel level, String message, Throwable throwable) {
        if (!owner.isLoggable(level)) {
            return;
        }
        if (level == LogLevel.WARN || level == LogLevel.ERROR) {
            System.err.println("[" + modId + "] [" + level + "] " + message);
        } else {
            System.out.println("[" + modId + "] [" + level + "] " + message);
        }
        if (throwable != null) {
            throwable.printStackTrace();
        }
    }
}