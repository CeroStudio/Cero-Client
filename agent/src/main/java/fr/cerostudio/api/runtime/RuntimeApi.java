package fr.cerostudio.api.runtime;

import java.lang.management.ManagementFactory;
import java.util.Locale;
import java.util.TimeZone;

public final class RuntimeApi {

    public RuntimeApi() {}

    public String javaVersion() {
        return System.getProperty("java.version", "inconnue");
    }

    public String javaVendor() {
        return System.getProperty("java.vendor", "inconnu");
    }

    public String osName() {
        return System.getProperty("os.name", "inconnu");
    }

    public String osArch() {
        return System.getProperty("os.arch", "inconnue");
    }

    public String os() {
        return osName() + " " + System.getProperty("os.version", "?") + " (" + osArch() + ")";
    }

    public long maxMemoryMb() {
        return Runtime.getRuntime().maxMemory() >> 20;
    }

    public long totalMemoryMb() {
        return Runtime.getRuntime().totalMemory() >> 20;
    }

    public long freeMemoryMb() {
        return Runtime.getRuntime().freeMemory() >> 20;
    }

    public long usedMemoryMb() {
        return totalMemoryMb() - freeMemoryMb();
    }

    public boolean is64Bit() {
        String dataModel = System.getProperty("sun.arch.data.model");
        if ("64".equals(dataModel)) {
            return true;
        }
        if ("32".equals(dataModel)) {
            return false;
        }
        String arch = System.getProperty("os.arch", "");
        return arch.endsWith("64");
    }

    public long jvmUptimeMs() {
        return ManagementFactory.getRuntimeMXBean().getUptime();
    }

    public long pid() {
        String name = ManagementFactory.getRuntimeMXBean().getName();
        int separator = name.indexOf('@');
        try {
            return Long.parseLong(separator > 0 ? name.substring(0, separator) : name);
        } catch (NumberFormatException e) {
            return -1L;
        }
    }

    public String gameDir() {
        return System.getProperty("user.dir", ".");
    }

    public String javaHome() {
        return System.getProperty("java.home", "inconnu");
    }

    public String locale() {
        return Locale.getDefault().toString();
    }

    public String timezone() {
        return TimeZone.getDefault().getID();
    }
}