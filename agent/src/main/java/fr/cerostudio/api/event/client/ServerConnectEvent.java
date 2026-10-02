package fr.cerostudio.api.event.client;

import fr.cerostudio.api.event.Event;

public final class ServerConnectEvent extends Event {

    private final String address;
    private final long timestampMs;

    public ServerConnectEvent(String address, long timestampMs) {
        this.address = address;
        this.timestampMs = timestampMs;
    }

    public String getAddress() {
        return address;
    }

    public long getTimestampMs() {
        return timestampMs;
    }
}