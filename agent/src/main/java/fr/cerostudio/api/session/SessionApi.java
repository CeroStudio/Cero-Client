package fr.cerostudio.api.session;

import fr.cerostudio.api.event.EventBus;
import fr.cerostudio.api.event.EventPriority;
import fr.cerostudio.api.event.client.GameStartEvent;

import java.util.function.Consumer;

public final class SessionApi {

    private final long bootstrapMs;
    private volatile long startedAtMs = -1L;
    private volatile String gameVersion;

    public SessionApi(final EventBus eventBus, long bootstrapMs) {
        this.bootstrapMs = bootstrapMs;
        eventBus.register(GameStartEvent.class, EventPriority.NORMAL, new Consumer<GameStartEvent>() {
            @Override
            public void accept(GameStartEvent event) {
                if (startedAtMs < 0L) {
                    startedAtMs = System.currentTimeMillis();
                    gameVersion = event.getMcVersion();
                }
            }
        });
    }

    public boolean isInGame() {
        return startedAtMs >= 0L;
    }

    public long startedAtMs() {
        return startedAtMs;
    }

    public long playtimeMs() {
        long start = startedAtMs;
        return start < 0L ? 0L : System.currentTimeMillis() - start;
    }

    public long sinceLauncherStartMs() {
        return System.currentTimeMillis() - bootstrapMs;
    }

    public String gameVersion() {
        return gameVersion;
    }
}