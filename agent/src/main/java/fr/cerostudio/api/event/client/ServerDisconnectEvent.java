package fr.cerostudio.api.event.client;

import fr.cerostudio.api.event.Event;

public final class ServerDisconnectEvent extends Event {

    private final long timestampMs;

    public ServerDisconnectEvent(long timestampMs) {
        this.timestampMs = timestampMs;
    }

    public long getTimestampMs() {
        return timestampMs;
    }
}