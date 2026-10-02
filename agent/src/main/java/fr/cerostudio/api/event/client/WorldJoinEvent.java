package fr.cerostudio.api.event.client;

import fr.cerostudio.api.event.Event;

public final class WorldJoinEvent extends Event {

    private final long timestampMs;

    public WorldJoinEvent(long timestampMs) {
        this.timestampMs = timestampMs;
    }

    public long getTimestampMs() {
        return timestampMs;
    }
}