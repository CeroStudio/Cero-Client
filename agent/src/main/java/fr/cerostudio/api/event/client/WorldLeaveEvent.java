package fr.cerostudio.api.event.client;

import fr.cerostudio.api.event.Event;

public final class WorldLeaveEvent extends Event {

    private final long timestampMs;

    public WorldLeaveEvent(long timestampMs) {
        this.timestampMs = timestampMs;
    }

    public long getTimestampMs() {
        return timestampMs;
    }
}