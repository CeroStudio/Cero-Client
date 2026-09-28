package fr.cerostudio.api.event.client;

import fr.cerostudio.api.event.Event;

public final class GameStopEvent extends Event {

    private final long timestampMs;

    public GameStopEvent(long timestampMs) {
        this.timestampMs = timestampMs;
    }

    public long getTimestampMs() {
        return timestampMs;
    }
}