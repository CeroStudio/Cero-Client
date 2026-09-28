package fr.cerostudio.api.config;

import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.Map;
import java.util.Properties;
import java.util.concurrent.locks.ReentrantReadWriteLock;

public final class ModConfig {

    private final Path file;
    private final String modId;
    private final Properties values = new Properties();
    private final ReentrantReadWriteLock lock = new ReentrantReadWriteLock();

    ModConfig(Path file, String modId) {
        this.file = file;
        this.modId = modId;
        reload();
    }

    public String modId() {
        return modId;
    }

    public Path file() {
        return file;
    }

    public String get(String key) {
        lock.readLock().lock();
        try {
            return values.getProperty(key);
        } finally {
            lock.readLock().unlock();
        }
    }

    public String get(String key, String defaultValue) {
        lock.readLock().lock();
        try {
            return values.getProperty(key, defaultValue);
        } finally {
            lock.readLock().unlock();
        }
    }

    public int getInt(String key, int defaultValue) {
        String raw = get(key);
        if (raw == null) {
            return defaultValue;
        }
        try {
            return Integer.parseInt(raw.trim());
        } catch (NumberFormatException e) {
            return defaultValue;
        }
    }

    public long getLong(String key, long defaultValue) {
        String raw = get(key);
        if (raw == null) {
            return defaultValue;
        }
        try {
            return Long.parseLong(raw.trim());
        } catch (NumberFormatException e) {
            return defaultValue;
        }
    }

    public boolean getBoolean(String key, boolean defaultValue) {
        String raw = get(key);
        if (raw == null) {
            return defaultValue;
        }
        String trimmed = raw.trim();
        return "true".equalsIgnoreCase(trimmed) || "1".equals(trimmed);
    }

    public double getDouble(String key, double defaultValue) {
        String raw = get(key);
        if (raw == null) {
            return defaultValue;
        }
        try {
            return Double.parseDouble(raw.trim());
        } catch (NumberFormatException e) {
            return defaultValue;
        }
    }

    public void set(String key, String value) {
        if (key == null) {
            throw new IllegalArgumentException("clé null");
        }
        lock.writeLock().lock();
        try {
            values.setProperty(key, value == null ? "" : value);
        } finally {
            lock.writeLock().unlock();
        }
    }

    public void remove(String key) {
        lock.writeLock().lock();
        try {
            values.remove(key);
        } finally {
            lock.writeLock().unlock();
        }
    }

    public Map<String, String> all() {
        Map<String, String> snapshot = new HashMap<>();
        lock.readLock().lock();
        try {
            Enumeration<?> names = values.propertyNames();
            while (names.hasMoreElements()) {
                String name = (String) names.nextElement();
                snapshot.put(name, values.getProperty(name));
            }
        } finally {
            lock.readLock().unlock();
        }
        return snapshot;
    }

    public boolean reload() {
        lock.writeLock().lock();
        try {
            values.clear();
            if (!Files.exists(file)) {
                return true;
            }
            InputStream stream = Files.newInputStream(file);
            try {
                values.load(stream);
                return true;
            } finally {
                stream.close();
            }
        } catch (Exception e) {
            return false;
        } finally {
            lock.writeLock().unlock();
        }
    }

    public boolean save() {
        lock.readLock().lock();
        try {
            Path parent = file.getParent();
            if (parent != null) {
                Files.createDirectories(parent);
            }
            OutputStream stream = Files.newOutputStream(file);
            try {
                values.store(stream, "CeroClient " + modId);
                return true;
            } finally {
                stream.close();
            }
        } catch (Exception e) {
            return false;
        } finally {
            lock.readLock().unlock();
        }
    }
}