package fr.cerostudio.api.scheduler;

public interface Cancellable {

    void cancel();

    boolean isCancelled();
}