package fr.cerostudio.api.logger;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public final class LoggerApi {

    private volatile LogLevel level = LogLevel.INFO;
    private final Map<String, CeroLogger> loggers = new ConcurrentHashMap<>();

    public LoggerApi() {}

    public CeroLogger of(String modId) {
        if (modId == null || modId.isEmpty()) {
            throw new IllegalArgumentException("modId invalide : " + modId);
        }
        return loggers.computeIfAbsent(modId, id -> new CeroLogger(id, this));
    }

    public void setLevel(LogLevel level) {
        if (level != null) {
            this.level = level;
        }
    }

    public LogLevel getLevel() {
        return level;
    }

    boolean isLoggable(LogLevel candidate) {
        return candidate.severity() >= level.severity();
    }
}