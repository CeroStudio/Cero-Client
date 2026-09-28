package fr.cerostudio.api.scheduler;

import fr.cerostudio.api.event.EventBus;
import fr.cerostudio.api.event.EventPriority;
import fr.cerostudio.api.event.client.ClientTickEvent;

import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.Consumer;
import java.util.logging.Level;
import java.util.logging.Logger;

public final class Scheduler {

    private static final Logger LOGGER = Logger.getLogger("CeroApi-Scheduler");

    private static final class Task implements Cancellable {

        final long period;
        final Runnable action;
        volatile boolean cancelled;
        long fireAt;

        Task(long fireAt, long period, Runnable action) {
            this.fireAt = fireAt;
            this.period = period;
            this.action = action;
        }

        @Override
        public void cancel() {
            cancelled = true;
        }

        @Override
        public boolean isCancelled() {
            return cancelled;
        }
    }

    private final List<Task> tasks = new CopyOnWriteArrayList<>();
    private final AtomicLong tickCounter = new AtomicLong();
    private final ExecutorService asyncExecutor;
    private volatile boolean shutdown = false;

    public Scheduler(final EventBus eventBus) {
        this.asyncExecutor = Executors.newSingleThreadExecutor(new java.util.concurrent.ThreadFactory() {
            @Override
            public Thread newThread(Runnable runnable) {
                Thread thread = new Thread(runnable, "Cero-Scheduler-Async");
                thread.setDaemon(true);
                return thread;
            }
        });

        eventBus.register(ClientTickEvent.class, EventPriority.NORMAL, new Consumer<ClientTickEvent>() {
            @Override
            public void accept(ClientTickEvent event) {
                if (event.getPhase() == ClientTickEvent.Phase.END) {
                    pump();
                }
            }
        });
    }

    public Cancellable runLater(long delayTicks, Runnable action) {
        if (action == null) {
            throw new IllegalArgumentException("action ne peut pas être null");
        }
        if (delayTicks < 0) {
            delayTicks = 0;
        }
        Task task = new Task(tickCounter.get() + delayTicks, 0L, action);
        tasks.add(task);
        return task;
    }

    public Cancellable runEvery(long periodTicks, Runnable action) {
        if (action == null) {
            throw new IllegalArgumentException("action ne peut pas être null");
        }
        if (periodTicks < 1) {
            throw new IllegalArgumentException("periodTicks doit être >= 1 (obtenu : " + periodTicks + ")");
        }
        Task task = new Task(tickCounter.get() + periodTicks, periodTicks, action);
        tasks.add(task);
        return task;
    }

    public void runAsync(Runnable action) {
        if (action == null) {
            throw new IllegalArgumentException("action ne peut pas être null");
        }
        if (shutdown) {
            LOGGER.warning("runAsync() ignoré : scheduler déjà arrêté");
            return;
        }
        asyncExecutor.execute(new Runnable() {
            @Override
            public void run() {
                try {
                    action.run();
                } catch (Throwable t) {
                    LOGGER.log(Level.WARNING, "Tâche async a levé une exception", t);
                }
            }
        });
    }

    public long currentTick() {
        return tickCounter.get();
    }

    public void shutdown() {
        if (shutdown) {
            return;
        }
        shutdown = true;
        tasks.clear();
        asyncExecutor.shutdown();
        LOGGER.info("Scheduler arrêté");
    }

    private void pump() {
        long now = tickCounter.incrementAndGet();

        for (Task task : tasks) {
            if (task.cancelled) {
                tasks.remove(task);
                continue;
            }
            if (task.fireAt > now) {
                continue;
            }

            if (task.period == 0L) {
                tasks.remove(task);
                runTask(task);
            } else {
                task.fireAt = now + task.period;
                runTask(task);
            }
        }
    }

    private void runTask(Task task) {
        try {
            task.action.run();
        } catch (Throwable t) {
            LOGGER.log(Level.WARNING, "Tâche planifiée a levé une exception", t);
        }
    }
}