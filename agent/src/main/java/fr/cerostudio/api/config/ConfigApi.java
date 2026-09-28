package fr.cerostudio.api.config;

import fr.cerostudio.api.event.EventBus;
import fr.cerostudio.api.event.EventPriority;
import fr.cerostudio.api.event.client.GameStopEvent;

import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.Consumer;

public final class ConfigApi {

    private static final int MAX_MOD_ID_LENGTH = 64;

    private final Path baseDir;
    private final Map<String, ModConfig> configs = new ConcurrentHashMap<>();

    public ConfigApi(final EventBus eventBus) {
        this(Paths.get(System.getProperty("user.dir"), "cero", "config"), eventBus);
    }

    ConfigApi(Path baseDir, final EventBus eventBus) {
        this.baseDir = baseDir;
        eventBus.register(GameStopEvent.class, EventPriority.NORMAL, new Consumer<GameStopEvent>() {
            @Override
            public void accept(GameStopEvent event) {
                saveAll();
            }
        });
    }

    public ModConfig of(String modId) {
        requireValidModId(modId);
        return configs.computeIfAbsent(modId, id -> new ModConfig(baseDir.resolve(id + ".properties"), id));
    }

    public int saveAll() {
        int saved = 0;
        for (ModConfig config : configs.values()) {
            if (config.save()) {
                saved++;
            }
        }
        return saved;
    }

    public int reloadAll() {
        int reloaded = 0;
        for (ModConfig config : configs.values()) {
            if (config.reload()) {
                reloaded++;
            }
        }
        return reloaded;
    }

    public boolean isValidModId(String modId) {
        if (modId == null || modId.isEmpty() || modId.length() > MAX_MOD_ID_LENGTH) {
            return false;
        }
        for (int i = 0; i < modId.length(); i++) {
            char c = modId.charAt(i);
            boolean allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                    || (c >= '0' && c <= '9') || c == '-' || c == '_';
            if (!allowed) {
                return false;
            }
        }
        return true;
    }

    private void requireValidModId(String modId) {
        if (!isValidModId(modId)) {
            throw new IllegalArgumentException("modId invalide : " + modId);
        }
    }
}